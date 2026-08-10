// host_test.cpp - runs the platform-independent half of the game (city
// generation, navigation, AI, player physics, missions, audio DSP, asset
// loading) on a desktop machine.
//
// This is not a substitute for playing it on a phone. It exists to catch the
// class of bug that is miserable to find on-device: an objective walled off
// inside a building, a monster walking through a wall, NaN leaking into a
// position, the mixer blowing up, a material array that has silently
// reshuffled. Build and run: tests/run_tests.sh
#include "../app/src/main/cpp/world.h"
#include "../app/src/main/cpp/actors.h"
#include "../app/src/main/cpp/missions.h"
#include "../app/src/main/cpp/player.h"
#include "../app/src/main/cpp/audio.h"
#include "../app/src/main/cpp/assets.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <queue>
#include <vector>
#include <algorithm>

// Structural validation of the pack lives in pak_verify.c and is exercised
// directly below.
extern "C" int hm_pak_validate(const void* base, size_t size, char* err, size_t errLen);

using namespace hm;

static int gFails = 0;
static int gChecks = 0;

static void check(bool cond, const char* what) {
    gChecks++;
    if (!cond) {
        gFails++;
        std::printf("  FAIL  %s\n", what);
    }
}

static bool finite3(const vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

static std::vector<bool> reachableFrom(const World& w, int sx, int sz) {
    std::vector<bool> seen((size_t)w.W * w.H, false);
    if (!w.inBounds(sx, sz) || w.solid(sx, sz)) return seen;
    std::queue<int> q;
    seen[sz * w.W + sx] = true;
    q.push(sz * w.W + sx);
    const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
    while (!q.empty()) {
        int cur = q.front(); q.pop();
        int cx = cur % w.W, cz = cur / w.W;
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], nz = cz + dz[d];
            if (!w.inBounds(nx, nz) || w.solid(nx, nz)) continue;
            if (seen[nz * w.W + nx]) continue;
            seen[nz * w.W + nx] = true;
            q.push(nz * w.W + nx);
        }
    }
    return seen;
}

// A bot that actually crosses the city, the way a player does. An earlier
// version of this test used a random-walk bot, which just vibrated near the
// spawn and made the AI look broken when it was the test that was wrong.
struct Bot {
    std::vector<int> field;
    int tx = -1, tz = -1;
    Rng rng;
    explicit Bot(uint32_t seed) : rng(seed) {}

    void buildField(const World& w) {
        field.assign((size_t)w.W * w.H, -1);
        std::queue<int> q;
        field[tz * w.W + tx] = 0;
        q.push(tz * w.W + tx);
        const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
        while (!q.empty()) {
            int cur = q.front(); q.pop();
            int cx = cur % w.W, cz = cur / w.W;
            for (int d = 0; d < 4; d++) {
                int nx = cx + dx[d], nz = cz + dz[d];
                if (!w.inBounds(nx, nz) || w.solid(nx, nz)) continue;
                if (field[nz * w.W + nx] >= 0) continue;
                field[nz * w.W + nx] = field[cur] + 1;
                q.push(nz * w.W + nx);
            }
        }
    }

    void retarget(const World& w, const vec3& from) {
        for (int tries = 0; tries < 400; tries++) {
            int x = rng.rangei(1, w.W - 1), z = rng.rangei(1, w.H - 1);
            if (w.solid(x, z)) continue;
            if (length(w.cellCenter(x, z) - from) < 25.0f) continue;
            tx = x; tz = z;
            buildField(w);
            return;
        }
    }

    void drive(const World& w, Player& p) {
        int cx, cz;
        w.worldToCell(p.pos, cx, cz);
        bool need = (tx < 0) || field.empty() || !w.inBounds(cx, cz) ||
                    field[cz * w.W + cx] < 0 || (cx == tx && cz == tz);
        if (need) {
            retarget(w, p.pos);
            if (tx < 0 || field.empty()) return;
            w.worldToCell(p.pos, cx, cz);
            if (!w.inBounds(cx, cz) || field[cz * w.W + cx] < 0) return;
        }
        int bx = cx, bz = cz, best = field[cz * w.W + cx];
        const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], nz = cz + dz[d];
            if (!w.inBounds(nx, nz) || w.solid(nx, nz)) continue;
            int f = field[nz * w.W + nx];
            if (f >= 0 && f < best) { best = f; bx = nx; bz = nz; }
        }
        vec3 d = w.cellCenter(bx, bz) - p.pos;
        d.y = 0.0f;
        if (lengthSq(d) > 1e-6f)
            p.yaw = angleTowards(p.yaw, std::atan2(d.x, d.z), 0.14f);
        p.moveInput = vec2(0.0f, 1.0f);
    }
};

// The world the game actually ships. Generated once and shared by the tests
// that only read it.
static World& city() {
    static World w;
    static bool built = false;
    if (!built) {
        w.generate(0x4A17E5u, 160, 160);
        built = true;
    }
    return w;
}

// ---------------------------------------------------------------------------

static void testCityLayout() {
    const World& w = city();
    std::printf("\n[city] layout\n");

    int counts[CELL_TYPE_COUNT] = {0};
    for (int i = 0; i < w.W * w.H; i++) counts[w.cells[i]]++;
    int walkable = w.W * w.H - counts[CELL_SOLID];
    std::printf("  %dx%d cells (%.0fm square), %d walkable\n", w.W, w.H,
                w.extentX(), walkable);
    std::printf("  road %d, pavement %d, lots %d, interior %d, doorways %d\n",
                counts[CELL_ROAD], counts[CELL_SIDEWALK], counts[CELL_LOT],
                counts[CELL_INTERIOR], counts[CELL_DOORWAY]);
    std::printf("  %d buildings (%d enterable), %d lights, %d props\n",
                (int)w.buildings.size(),
                (int)std::count_if(w.buildings.begin(), w.buildings.end(),
                                   [](const Building& b) { return b.enterable; }),
                (int)w.lights.size(), (int)w.props.size());

    check(walkable > w.W * w.H / 5, "the city is not one solid block");
    check(counts[CELL_ROAD] > 1000, "there are streets");
    check(counts[CELL_SIDEWALK] > 500, "there are pavements");
    check(counts[CELL_INTERIOR] > 500, "buildings have interiors");
    check(!w.buildings.empty(), "buildings were placed");

    bool sealed = true;
    for (int x = 0; x < w.W; x++)
        if (!w.solid(x, 0) || !w.solid(x, w.H - 1)) sealed = false;
    for (int z = 0; z < w.H; z++)
        if (!w.solid(0, z) || !w.solid(w.W - 1, z)) sealed = false;
    check(sealed, "the district border is sealed");

    int psx, psz;
    w.worldToCell(w.playerStart, psx, psz);
    check(!w.solid(psx, psz), "player spawn is not inside a wall");

    // Everything walkable must be reachable, or the player can see rooms and
    // streets they can never get to.
    std::vector<bool> reach = reachableFrom(w, psx, psz);
    int stranded = 0;
    for (int i = 0; i < w.W * w.H; i++)
        if (cellWalkable(w.cells[i]) && !reach[i]) stranded++;
    float strandedPct = 100.0f * stranded / std::max(1, walkable);
    std::printf("  unreachable walkable cells: %d (%.2f%%)\n", stranded, strandedPct);
    check(stranded == 0, "every walkable cell is reachable from the spawn");

    // Every enterable building must actually be enterable.
    int badDoors = 0, enterable = 0;
    for (const Building& b : w.buildings) {
        if (!b.enterable) continue;
        enterable++;
        if (b.doorCX < 0 || !cellIndoor(w.at(b.doorCX, b.doorCZ))) { badDoors++; continue; }
        if (!reach[b.doorCZ * w.W + b.doorCX]) badDoors++;
    }
    check(enterable > 4, "a meaningful number of buildings can be entered");
    check(badDoors == 0, "every enterable building has a reachable door");

    int badProps = 0;
    for (const Prop& p : w.props) {
        int cx, cz;
        w.worldToCell(p.pos, cx, cz);
        if (w.solid(cx, cz)) badProps++;
    }
    check(badProps == 0, "no prop is buried inside a wall");

    int badLights = 0;
    for (const LightSrc& L : w.lights)
        if (!std::isfinite(L.pos.x) || L.radius <= 0.0f) badLights++;
    check(badLights == 0, "every light is finite and has a radius");
}

static void testChunks() {
    const World& w = city();
    std::printf("\n[chunks] meshing\n");
    std::vector<Chunk> chunks;
    w.buildChunks(chunks, 24);

    size_t verts = 0, tris = 0;
    int badIdx = 0, badNormal = 0, badTangent = 0, badMat = 0, badBounds = 0;
    for (const Chunk& c : chunks) {
        verts += c.mesh.verts.size();
        tris += c.mesh.triCount();
        for (uint32_t i : c.mesh.idx)
            if (i >= c.mesh.verts.size()) badIdx++;
        for (const Vertex& v : c.mesh.verts) {
            float nl = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
            float tl = std::sqrt(v.tx * v.tx + v.ty * v.ty + v.tz * v.tz);
            if (!(nl > 0.5f && nl < 1.5f)) badNormal++;
            if (!(tl > 0.5f && tl < 1.5f)) badTangent++;
            // The shader builds a TBN from these; a tangent parallel to the
            // normal would collapse it and light the surface from nowhere.
            float dotNT = v.nx * v.tx + v.ny * v.ty + v.nz * v.tz;
            if (std::fabs(dotNT) > 0.35f) badTangent++;
            if (v.mat < 0.0f || v.mat >= (float)MAT_COUNT) badMat++;
            if (v.px < c.mn.x - 0.5f || v.px > c.mx.x + 0.5f ||
                v.pz < c.mn.z - 0.5f || v.pz > c.mx.z + 0.5f ||
                v.py > c.mx.y + 0.5f) badBounds++;
        }
    }
    std::printf("  %d chunks, %d verts, %d tris (%.1f tris/chunk)\n",
                (int)chunks.size(), (int)verts, (int)tris,
                tris / (float)std::max<size_t>(1, chunks.size()));

    check(!chunks.empty(), "the city meshed into chunks");
    check(tris > 20000, "the city has real geometry in it");
    check(badIdx == 0, "no index points past its vertex array");
    check(badNormal == 0, "every normal is unit length");
    check(badTangent == 0, "every tangent is unit length and not parallel to the normal");
    check(badMat == 0, "every material index is inside the texture array");
    check(badBounds == 0, "chunk bounds actually contain the chunk's geometry");
}

// The material table is duplicated: the baker writes layers in registry order,
// matids.h names them for the runtime. If those drift the whole world silently
// retextures, so the pack itself is the source of truth here.
static void testMaterialTable(const char* pakPath) {
    std::printf("\n[materials] pack agrees with matids.h\n");
    AssetPack pack;
    if (!pack.openFile(pakPath)) {
        std::printf("  (pack not found at %s - skipping)\n", pakPath);
        return;
    }
    int missing = 0, wrongSize = 0;
    int size = 0;
    for (int i = 0; i < MAT_COUNT; i++) {
        char an[64], nn[64];
        std::snprintf(an, sizeof(an), "%s_a", kMatNames[i]);
        std::snprintf(nn, sizeof(nn), "%s_n", kMatNames[i]);
        const PakEntry* a = pack.find(an);
        const PakEntry* n = pack.find(nn);
        if (!a || !n) { missing++; std::printf("  missing: %s\n", kMatNames[i]); continue; }
        if (size == 0) size = (int)a->w;
        if ((int)a->w != size || (int)a->h != size ||
            (int)n->w != size || (int)n->h != size) wrongSize++;
    }
    std::printf("  %d materials at %dx%d, pack is %.1f MB\n",
                MAT_COUNT, size, size, pack.totalBytes() / (1024.0 * 1024.0));
    check(missing == 0, "every material named in matids.h exists in the pack");
    check(wrongSize == 0, "every material layer is the same size");
    check(size >= 256, "materials are a usable resolution");

    // Voice and ambience the game asks for by name at runtime.
    const char* needed[] = {
        "amb_street", "amb_interior", "amb_tunnels",
        "vo_dispatch_open", "vo_objective_done", "vo_sv_follow", "vo_sv_thanks",
        "vo_warn_close", "vo_warn_dark", "vo_good", "vo_lost_signal",
        "vx_chitter_0", "vx_wail_0", "vx_sob_0", "vo_sv_help", "vo_sv_run",
        "sfx_creak_door_0", "sfx_creak_door_3", "sfx_creak_floor_0",
        "sfx_creak_metal_0", "sfx_groan_struct_0",
    };
    int missingAudio = 0;
    for (const char* nm : needed) {
        if (!pack.find(nm)) { missingAudio++; std::printf("  missing audio: %s\n", nm); }
    }
    check(missingAudio == 0, "every sound the game asks for by name is in the pack");

    // Every mission's briefing line must exist, or a mission starts in silence.
    int missingBrief = 0;
    for (int i = 0; i < MISSION_COUNT; i++)
        if (!pack.find(missionDef(i).briefVoice)) {
            missingBrief++;
            std::printf("  missing brief: %s\n", missionDef(i).briefVoice);
        }
    check(missingBrief == 0, "every mission has its briefing audio");
}

static void testMissions() {
    World& w = city();
    std::printf("\n[missions] the chain\n");
    int unreachableTotal = 0, emptyTotal = 0;

    int psx, psz;
    w.worldToCell(w.playerStart, psx, psz);
    std::vector<bool> reach = reachableFrom(w, psx, psz);

    vec3 anchor = w.playerStart;
    for (int i = 0; i < MISSION_COUNT; i++) {
        MissionState st;
        Rng rng(0x9E11u + i * 7717u);
        buildMission(st, i, w, anchor, rng);
        const MissionDef& d = missionDef(i);

        if (st.objectives.empty()) { emptyTotal++; continue; }
        int bad = 0;
        for (const Objective& o : st.objectives) {
            int cx, cz;
            w.worldToCell(o.pos, cx, cz);
            if (!w.inBounds(cx, cz) || w.solid(cx, cz) || !reach[cz * w.W + cx]) bad++;
        }
        if (d.type == MT_RESCUE) {
            int cx, cz;
            w.worldToCell(st.dropOff, cx, cz);
            if (!w.inBounds(cx, cz) || w.solid(cx, cz) || !reach[cz * w.W + cx]) bad++;
        }
        unreachableTotal += bad;
        float dist = length(st.marker - anchor);
        std::printf("  %02d %-14s %-10s %d objective(s), marker %.0fm  %s\n",
                    i + 1, d.title,
                    d.type == MT_GOTO ? "goto" : d.type == MT_COLLECT ? "collect" :
                    d.type == MT_ACTIVATE ? "activate" : d.type == MT_RESCUE ? "rescue" :
                    d.type == MT_SURVIVE ? "survive" : "escape",
                    (int)st.objectives.size(), dist, bad ? "UNREACHABLE" : "");
        anchor = st.marker;
    }
    check(emptyTotal == 0, "every mission produced at least one objective");
    check(unreachableTotal == 0, "every mission objective is reachable on foot");
}

static void testCollision() {
    const World& w = city();
    std::printf("\n[collision] wall resolution\n");
    Rng r(0xDEAD);
    int escaped = 0, trials = 0;
    for (int i = 0; i < 60000; i++) {
        int cx = r.rangei(1, w.W - 1), cz = r.rangei(1, w.H - 1);
        if (w.solid(cx, cz)) continue;
        trials++;
        vec3 from = w.cellCenter(cx, cz);
        float a = r.range(0.0f, TAU);
        vec3 to = from + vec3(std::cos(a), 0, std::sin(a)) * r.range(0.1f, 6.0f);
        vec3 out = w.resolveCollision(to, Player::RADIUS);
        int ox, oz;
        w.worldToCell(out, ox, oz);
        if (w.solid(ox, oz)) escaped++;
    }
    std::printf("  %d shove tests, %d ended inside a wall\n", trials, escaped);
    check(trials > 5000, "collision test had enough samples");
    check(escaped == 0, "collision resolution never leaves anything inside a wall");
}

static void testLineOfSight() {
    const World& w = city();
    std::printf("\n[sight] line of sight\n");
    Rng r(0x5EED);
    int asym = 0, throughWall = 0, trials = 0;
    for (int i = 0; i < 40000; i++) {
        // Pick a cell, then a second one nearby: two independent random cells
        // in a 160x160 grid are almost always further apart than anything can
        // see, so that sampling barely tests the traversal at all.
        int ax = r.rangei(1, w.W - 1), az = r.rangei(1, w.H - 1);
        int bx = ax + r.rangei(-14, 15), bz = az + r.rangei(-14, 15);
        if (!w.inBounds(bx, bz)) continue;
        if (w.solid(ax, az) || w.solid(bx, bz)) continue;
        trials++;
        vec3 a = w.cellCenter(ax, az) + vec3(0, 1.5f, 0);
        vec3 b = w.cellCenter(bx, bz) + vec3(0, 1.5f, 0);
        bool ab = w.lineOfSight(a, b), ba = w.lineOfSight(b, a);
        if (ab != ba) asym++;
        if (ab) {
            int mx = (ax + bx) / 2, mz = (az + bz) / 2;
            if (w.solid(mx, mz) && (std::abs(ax - bx) + std::abs(az - bz)) > 3) throughWall++;
        }
    }
    std::printf("  %d trials, %d asymmetric, %d through a solid midpoint\n",
                trials, asym, throughWall);
    check(trials > 2000, "sight test had enough samples");
    check(asym == 0, "line of sight is symmetric");
    check(throughWall == 0, "line of sight never crosses a solid midpoint");
}

static void testRigs() {
    std::printf("\n[actors] rigs and skeletons\n");
    const char* names[AK_KIND_COUNT] = {"stalker", "crawler", "watcher", "survivor"};
    for (int k = 0; k < AK_KIND_COUNT; k++) {
        Mesh parts[BP_COUNT];
        buildActorParts((ActorKind)k, parts);
        int verts = 0, tris = 0, badMat = 0;
        for (int i = 0; i < BP_COUNT; i++) {
            check(!parts[i].verts.empty(), "every body part has geometry");
            verts += (int)parts[i].verts.size();
            tris += (int)parts[i].triCount();
            for (const Vertex& v : parts[i].verts)
                if (v.mat < 0.0f || v.mat >= (float)MAT_COUNT) badMat++;
        }
        check(badMat == 0, "actor materials are inside the texture array");

        Monster m;
        m.spawn(city(), vec3(0, 0, 0), (ActorKind)k, 7u);
        m.pos = vec3(0, 0, 0);
        m.yaw = 0.0f;
        m.buildPose(0.0f);

        const Rig& rig = rigFor((ActorKind)k);
        vec3 head = m.parts[BP_HEAD].transformPoint(vec3(0, rig.headLen * 0.75f, 0));
        vec3 pelvis = m.parts[BP_PELVIS].transformPoint(vec3(0, 0, 0));
        vec3 foot = m.parts[BP_SHIN_L].transformPoint(vec3(0, rig.shin, 0));
        std::printf("  %-8s %4d verts %4d tris   head %.2fm  pelvis %.2fm  foot %.2fm\n",
                    names[k], verts, tris, head.y, pelvis.y, foot.y);

        check(head.y > pelvis.y + 0.3f, "the head sits above the pelvis");
        check(foot.y < 0.45f, "the feet reach the ground");
        for (int i = 0; i < BP_COUNT; i++)
            check(finite3(m.parts[i].transformPoint(vec3(0, 0, 0))), "bone transforms are finite");
    }

    // The three monsters must be visibly different heights, or the silhouette
    // stops telling the player which one is coming.
    float heights[3];
    for (int k = 0; k < 3; k++) {
        Monster m;
        m.spawn(city(), vec3(0, 0, 0), (ActorKind)k, 7u);
        m.pos = vec3(0, 0, 0);
        m.buildPose(0.0f);
        heights[k] = m.parts[BP_HEAD].transformPoint(vec3(0, rigFor((ActorKind)k).headLen, 0)).y;
    }
    std::printf("  silhouette heights: stalker %.2fm, crawler %.2fm, watcher %.2fm\n",
                heights[0], heights[1], heights[2]);
    check(heights[0] > 2.0f, "the stalker looms over a 1.66m player");
    check(heights[1] < heights[0] - 0.6f, "the crawler is much lower than the stalker");
    check(heights[2] > heights[0], "the watcher is the tallest thing in the city");
}

static void testMonsterSim(ActorKind kind, const char* name) {
    World& w = city();
    std::printf("\n[sim] %s - 240s\n", name);

    AudioEngine audio;
    audio.start();
    Player p;
    p.reset(w.playerStart);
    Monster m;
    m.spawn(w, w.cellCenter(w.W / 2 + 30, w.H / 2 + 30), kind, 0x1234u);
    Bot bot(0xA11CEu + kind);

    const float dt = 1.0f / 60.0f;
    int caught = 0, insideWall = 0, nonFinite = 0;
    int stateSeen[AS_STATE_COUNT] = {0};
    float minDist = 1e9f;

    for (int frame = 0; frame < 60 * 240; frame++) {
        bot.drive(w, p);
        p.sprintHeld = (frame / 300) % 4 == 0;
        p.crouchHeld = (frame / 300) % 4 == 2;

        float cd = m.distanceTo(p.pos);
        p.update(dt, w, audio, cd, false);

        vec3 pf = p.forward();
        pf.y = 0.0f;
        pf = normalize(pf);
        bool indoors = w.indoorAt(p.pos);
        if (m.update(dt, w, p.pos, pf, p.noise, p.torchOn, audio, indoors)) {
            caught++;
            p.reset(w.playerStart);
            m.spawn(w, w.cellCenter(w.W / 2 + 30, w.H / 2 + 30), kind, 0x1234u + caught);
            bot.tx = -1;
            continue;
        }

        stateSeen[m.state]++;
        minDist = std::min(minDist, cd);
        if (!finite3(p.pos) || !finite3(m.pos) || !std::isfinite(p.yaw) || !std::isfinite(m.yaw))
            nonFinite++;
        int cx, cz;
        w.worldToCell(p.pos, cx, cz);
        if (w.solid(cx, cz)) insideWall++;
        w.worldToCell(m.pos, cx, cz);
        if (w.solid(cx, cz)) insideWall++;
        if (frame % 8 == 0) audio.enqueueNext();
    }

    std::printf("  caught %d times, closest %.2fm\n", caught, minDist);
    std::printf("  wander=%d investigate=%d stalk=%d hunt=%d search=%d\n",
                stateSeen[AS_WANDER], stateSeen[AS_INVESTIGATE], stateSeen[AS_STALK],
                stateSeen[AS_HUNT], stateSeen[AS_SEARCH]);

    check(nonFinite == 0, "no NaN in any position or angle");
    check(insideWall == 0, "nothing ends up inside a wall");
    check(p.stamina >= 0.0f && p.stamina <= 1.0f, "stamina stays in range");
    check(p.battery >= 0.0f && p.battery <= 1.0f, "battery stays in range");

    if (kind == AK_WATCHER) {
        // It is rooted, so it must never move and never catch anyone.
        check(caught == 0, "the watcher never catches the player itself");
    } else {
        check(stateSeen[AS_HUNT] > 0, "it commits to a hunt at least once");
        check(caught > 0, "it is capable of catching a careless player");
    }
}

static void testWatcherAlerts() {
    World& w = city();
    std::printf("\n[watcher] calling the others in\n");
    AudioEngine audio;
    audio.start();

    Monster watcher, stalker;
    vec3 origin = w.playerStart;
    watcher.spawn(w, origin + vec3(6, 0, 0), AK_WATCHER, 11u);
    stalker.spawn(w, origin + vec3(60, 0, 60), AK_STALKER, 12u);
    stalker.state = AS_WANDER;

    ActorState before = stalker.state;
    watcher.lastKnownPlayer = origin;
    stalker.alertTo(origin);
    std::printf("  stalker state %d -> %d, target %.0fm from the player\n",
                (int)before, (int)stalker.state, length(stalker.targetPos - origin));

    check(stalker.state == AS_INVESTIGATE, "an alert pulls a patrolling stalker into investigating");
    check(length(stalker.targetPos - origin) < 1.0f, "the alert hands over the player's position");

    // A watcher must never be pulled off its post by another watcher.
    Monster w2;
    w2.spawn(w, origin + vec3(20, 0, 0), AK_WATCHER, 13u);
    ActorState wBefore = w2.state;
    w2.alertTo(origin);
    check(w2.state == wBefore, "watchers ignore alerts and hold their post");
    audio.stop();
}

static void testFlareStun() {
    World& w = city();
    std::printf("\n[flare] stun behaviour\n");
    AudioEngine audio;
    audio.start();
    Monster m;
    m.spawn(w, w.playerStart + vec3(4, 0, 0), AK_STALKER, 21u);
    m.state = AS_HUNT;
    m.stun(3.0f);

    Player p;
    p.reset(w.playerStart);
    vec3 pf(0, 0, 1);
    const float dt = 1.0f / 60.0f;
    vec3 startPos = m.pos;
    bool caughtWhileStunned = false;
    for (int i = 0; i < 60 * 2; i++)
        if (m.update(dt, w, p.pos, pf, 1.0f, true, audio, false)) caughtWhileStunned = true;

    float moved = length(m.pos - startPos);
    std::printf("  moved %.3fm while stunned\n", moved);
    check(!caughtWhileStunned, "a stunned monster cannot reach the player");
    check(moved < 0.05f, "a stunned monster does not move");

    // ...and it must recover, or the flare would be a kill button.
    for (int i = 0; i < 60 * 3; i++) m.update(dt, w, p.pos, pf, 1.0f, true, audio, false);
    check(m.stunTimer <= 0.0f, "the stun wears off");
    audio.stop();
}

static void testNpcs() {
    World& w = city();
    std::printf("\n[npc] survivors\n");
    AudioEngine audio;
    audio.start();

    Npc n;
    vec3 home = w.playerStart + vec3(8, 0, 0);
    vec3 open;
    Rng r(5u);
    if (w.findOpenNear(home, 20.0f, open, r)) home = open;
    n.spawn(home, 0, 99u);

    Player p;
    p.reset(w.playerStart);
    const float dt = 1.0f / 60.0f;
    vec3 farMonster = w.playerStart + vec3(500, 0, 500);

    // Idle: it should stay put and wait to be found.
    for (int i = 0; i < 60 * 6; i++) n.update(dt, w, p.pos, farMonster, 500.0f, audio, false);
    float drift = length(n.pos - home);
    std::printf("  idle drift %.2fm\n", drift);
    check(drift < 1.5f, "an unrescued survivor stays where they are");
    check(finite3(n.pos), "npc position stays finite");

    // Following: it should close on the player and then hold station.
    n.rescued = true;
    vec3 target = w.playerStart;
    Rng r2(6u);
    w.findOpenNear(w.playerStart + vec3(28, 0, 0), 25.0f, target, r2);
    for (int i = 0; i < 60 * 40; i++) {
        p.pos = target;
        n.update(dt, w, p.pos, farMonster, 500.0f, audio, true);
    }
    float follow = n.distanceTo(p.pos);
    std::printf("  follow distance %.2fm\n", follow);
    check(follow < 6.0f, "a rescued survivor follows the player");
    int cx, cz;
    w.worldToCell(n.pos, cx, cz);
    check(!w.solid(cx, cz), "the survivor never ends up inside a wall");

    // Fleeing: a monster on top of them must push them away, not through a wall.
    vec3 threat = n.pos + vec3(1.5f, 0, 0);
    vec3 fleeStart = n.pos;
    for (int i = 0; i < 60 * 4; i++) n.update(dt, w, p.pos, threat, 1.5f, audio, true);
    std::printf("  fled %.2fm from a monster at 1.5m\n", length(n.pos - fleeStart));
    check(n.state == AS_FLEE, "a survivor panics when a monster is on top of them");
    w.worldToCell(n.pos, cx, cz);
    check(!w.solid(cx, cz), "a fleeing survivor stays out of walls");
    audio.stop();
}

// Crouching must keep the monster off you far more than sprinting does.
// Death count is a poor measure of this - the quiet player also moves slower
// and so eats more random encounters - so measure detection instead.
static void testStealthMatters() {
    World& w = city();
    std::printf("\n[stealth] does staying quiet help?\n");

    struct Result { int pursued = 0; int total = 0; float firstHunt = -1.0f; };
    auto run = [&](bool sprintAlways, uint32_t seed) {
        AudioEngine audio;
        audio.start();
        Player p;
        p.reset(w.playerStart);
        Monster m;
        m.spawn(w, w.cellCenter(w.W / 2 + 25, w.H / 2 + 25), AK_STALKER, seed);
        Bot bot(seed);
        const float dt = 1.0f / 60.0f;
        Result res;
        for (int frame = 0; frame < 60 * 300; frame++) {
            bot.drive(w, p);
            p.sprintHeld = sprintAlways;
            p.crouchHeld = !sprintAlways;
            p.update(dt, w, audio, m.distanceTo(p.pos), false);
            vec3 pf = p.forward();
            pf.y = 0;
            pf = normalize(pf);
            if (m.update(dt, w, p.pos, pf, p.noise, false, audio, false)) {
                p.reset(w.playerStart);
                m.spawn(w, w.cellCenter(w.W / 2 + 25, w.H / 2 + 25), AK_STALKER, seed);
                bot.tx = -1;
                continue;
            }
            res.total++;
            if (m.state == AS_HUNT || m.state == AS_STALK || m.state == AS_INVESTIGATE) {
                res.pursued++;
                if (res.firstHunt < 0.0f) res.firstHunt = frame * dt;
            }
            if (frame % 8 == 0) audio.enqueueNext();
        }
        audio.stop();
        return res;
    };

    int loud = 0, loudT = 0, quiet = 0, quietT = 0;
    float loudFirst = 0, quietFirst = 0;
    const int seeds = 5;
    for (uint32_t s = 1; s <= (uint32_t)seeds; s++) {
        Result l = run(true, 0x2000 + s);
        Result q = run(false, 0x2000 + s);
        loud += l.pursued;   loudT += l.total;
        quiet += q.pursued;  quietT += q.total;
        loudFirst += (l.firstHunt < 0) ? 300.0f : l.firstHunt;
        quietFirst += (q.firstHunt < 0) ? 300.0f : q.firstHunt;
    }
    float loudPct = 100.0f * loud / std::max(1, loudT);
    float quietPct = 100.0f * quiet / std::max(1, quietT);
    std::printf("  sprinting: pursued %.1f%% of the time, first found at %.0fs\n",
                loudPct, loudFirst / seeds);
    std::printf("  crouching: pursued %.1f%% of the time, first found at %.0fs\n",
                quietPct, quietFirst / seeds);
    check(loudPct > quietPct * 1.4f, "sprinting keeps the monster on you far more");
    check(loudFirst < quietFirst, "sprinting gets you found sooner");
}

static void testAudio(const char* pakPath) {
    std::printf("\n[audio] mixer\n");
    AssetPack pack;
    bool havePack = pack.openFile(pakPath);

    AudioEngine a;
    if (havePack) a.setPack(&pack);
    a.start();
    a.setListener(vec3(0, 1.6f, 0), vec3(0, 0, 1));
    a.setTension(0.8f);
    a.setHeartRate(140.0f);
    a.setBreath(0.7f);
    a.setIndoor(1.0f);

    if (havePack) {
        int bed = a.sampleIndex("amb_street");
        int line = a.sampleIndex("vo_dispatch_open");
        std::printf("  samples resolved: bed=%d voice=%d\n", bed, line);
        check(bed >= 0, "the ambience bed resolves from the pack");
        check(line >= 0, "a voice line resolves from the pack");
        a.setBed(bed);
        a.postVoice(line, 0.9f);
    }

    const int frames = AudioEngine::BLOCK_FRAMES;
    std::vector<int16_t> buf(frames * 2);
    double peak = 0, energy = 0;
    long samples = 0;
    int nonFinite = 0;
    for (int block = 0; block < 400; block++) {
        if (block % 3 == 0)
            for (int id = 0; id < SND_COUNT; id++)
                a.post((SoundId)id, vec3((float)(id % 7) - 3.0f, 1.0f, (float)(id % 5)), 1.0f, 1.0f);
        a.renderBlock(buf.data(), frames);
        for (int i = 0; i < frames * 2; i++) {
            double v = buf[i] / 32768.0;
            if (!std::isfinite(v)) nonFinite++;
            peak = std::max(peak, std::fabs(v));
            energy += v * v;
            samples++;
        }
    }
    double rms = std::sqrt(energy / (double)samples);
    std::printf("  peak %.3f, rms %.4f over %ld samples\n", peak, rms, samples);
    check(nonFinite == 0, "the mixer never emits a non-finite sample");
    check(peak > 0.05, "the mixer produces audible signal");
    check(peak <= 1.0, "the mixer stays inside full scale");
    check(rms > 0.001 && rms < 0.5, "the mixer level is sane");

    AudioEngine b;
    b.start();
    b.setTension(0.0f);
    b.setHeartRate(50.0f);
    b.setBreath(0.0f);
    for (int i = 0; i < 200; i++) b.renderBlock(buf.data(), frames);
    double quietPeak = 0;
    for (int i = 0; i < 20; i++) {
        b.renderBlock(buf.data(), frames);
        for (int k = 0; k < frames * 2; k++)
            quietPeak = std::max(quietPeak, std::fabs(buf[k] / 32768.0));
    }
    std::printf("  idle peak %.4f\n", quietPeak);
    check(quietPeak < 0.35, "the reverb tank does not run away when nothing plays");
    b.stop();
    a.stop();
}

// The pack reader is the one place a mapped blob is trusted. Feed it corrupt
// input and confirm it refuses rather than walking off the end.
static void testPackValidation(const char* pakPath) {
    std::printf("\n[pack] rejects corruption\n");
    FILE* f = std::fopen(pakPath, "rb");
    if (!f) { std::printf("  (pack not found - skipping)\n"); return; }
    std::vector<uint8_t> good(4096);
    size_t n = std::fread(good.data(), 1, good.size(), f);
    std::fseek(f, 0, SEEK_END);
    long total = std::ftell(f);
    std::fclose(f);
    good.resize(n);

    char err[128];
    check(hm_pak_validate(nullptr, 0, err, sizeof(err)) == 0, "a null mapping is rejected");
    check(hm_pak_validate(good.data(), 4, err, sizeof(err)) == 0, "a truncated header is rejected");

    // Right header, but the file is far shorter than the directory claims.
    check(hm_pak_validate(good.data(), good.size(), err, sizeof(err)) == 0,
          "a pack truncated before its data is rejected");

    std::vector<uint8_t> bad = good;
    bad[0] ^= 0xFF;
    check(hm_pak_validate(bad.data(), bad.size(), err, sizeof(err)) == 0,
          "a bad magic number is rejected");

    // And the real file must pass.
    AssetPack pack;
    bool ok = pack.openFile(pakPath);
    std::printf("  real pack (%.1f MB): %s\n", total / (1024.0 * 1024.0), ok ? "accepted" : "REJECTED");
    check(ok, "the shipped pack passes validation");
}

int main(int argc, char** argv) {
    const char* pak = (argc > 1) ? argv[1] : "app/src/main/assets/hollow.pak";
    std::printf("=== HOLLOW SIGNAL - host test suite ===\n");

    testCityLayout();
    testChunks();
    testMaterialTable(pak);
    testMissions();
    testCollision();
    testLineOfSight();
    testRigs();
    testWatcherAlerts();
    testFlareStun();
    testNpcs();
    testMonsterSim(AK_STALKER, "stalker");
    testMonsterSim(AK_CRAWLER, "crawler");
    testMonsterSim(AK_WATCHER, "watcher");
    testStealthMatters();
    testAudio(pak);
    testPackValidation(pak);

    std::printf("\n=== %d checks, %d failures ===\n", gChecks, gFails);
    return gFails == 0 ? 0 : 1;
}
