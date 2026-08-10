// host_test.cpp - runs the platform-independent half of the game (level
// generation, navigation, AI, player physics, audio DSP) on a desktop machine.
//
// This is not a substitute for playing it on a phone, but it does catch the
// class of bug that is miserable to find on-device: unreachable objectives,
// the stalker walking through walls, NaN leaking into a position, the mixer
// blowing up. Build and run: see tests/run_tests.sh
#include "../app/src/main/cpp/world.h"
#include "../app/src/main/cpp/entity.h"
#include "../app/src/main/cpp/player.h"
#include "../app/src/main/cpp/audio.h"
#include <cstdio>
#include <cmath>
#include <queue>
#include <vector>

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

// Flood fill from a cell; returns the set of reachable floor cells.
static std::vector<bool> reachableFrom(const World& w, int sx, int sz) {
    std::vector<bool> seen(w.W * w.H, false);
    if (w.solid(sx, sz)) return seen;
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


// A bot that actually crosses the map, the way a player does. The first
// version of this test used a random-walk bot, which just vibrated near the
// spawn and made the AI look broken when it was the test that was wrong.
struct Bot {
    std::vector<int> field;
    int tx = -1, tz = -1;
    Rng rng;
    explicit Bot(uint32_t seed) : rng(seed) {}

    void buildField(const World& w) {
        field.assign(w.W * w.H, -1);
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
        for (int tries = 0; tries < 300; tries++) {
            int x = rng.rangei(1, w.W - 1), z = rng.rangei(1, w.H - 1);
            if (w.solid(x, z)) continue;
            if (length(w.cellCenter(x, z) - from) < 12.0f) continue;
            tx = x; tz = z;
            buildField(w);
            return;
        }
    }

    void drive(const World& w, Player& p) {
        int cx, cz;
        w.worldToCell(p.pos, cx, cz);
        bool needTarget = (tx < 0) || field.empty() ||
                          !w.inBounds(cx, cz) || field[cz * w.W + cx] < 0 ||
                          (cx == tx && cz == tz);
        if (needTarget) {
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
        if (lengthSq(d) > 1e-6f) {
            float want = std::atan2(d.x, d.z);
            p.yaw = angleTowards(p.yaw, want, 0.14f);
        }
        p.moveInput = vec2(0.0f, 1.0f);
    }
};

// ---------------------------------------------------------------------------

static void testChapter(int ci) {
    const ChapterSpec& spec = chapterSpec(ci);
    std::printf("\n[chapter %d] %s\n", ci + 1, spec.title);

    World w;
    w.generate(spec);

    int floorCells = 0;
    for (int i = 0; i < w.W * w.H; i++) if (w.cells[i] != CELL_SOLID) floorCells++;
    std::printf("  grid %dx%d, %d walkable cells, %d pickups, %d lights, %d props\n",
                w.W, w.H, floorCells, (int)w.pickups.size(),
                (int)w.lights.size(), (int)w.props.size());

    check(floorCells > w.W * w.H / 6, "level is not mostly solid rock");
    check((int)w.pickups.size() == spec.objectiveCount, "all objectives were placed");

    // The border must be sealed or the player walks off the edge of the world.
    bool sealed = true;
    for (int x = 0; x < w.W; x++)
        if (!w.solid(x, 0) || !w.solid(x, w.H - 1)) sealed = false;
    for (int z = 0; z < w.H; z++)
        if (!w.solid(0, z) || !w.solid(w.W - 1, z)) sealed = false;
    check(sealed, "outer border is sealed");

    // Everything the player must touch has to be reachable from the spawn.
    int psx, psz;
    w.worldToCell(w.playerStart, psx, psz);
    check(!w.solid(psx, psz), "player spawn is not inside a wall");
    std::vector<bool> reach = reachableFrom(w, psx, psz);

    check(reach[w.exitCZ * w.W + w.exitCX], "exit is reachable from spawn");
    int unreachable = 0;
    for (const Pickup& p : w.pickups) {
        int cx, cz;
        w.worldToCell(p.pos, cx, cz);
        if (!w.inBounds(cx, cz) || w.solid(cx, cz) || !reach[cz * w.W + cx]) unreachable++;
    }
    check(unreachable == 0, "every objective is reachable from spawn");

    // No prop may be embedded in a wall, or it will look broken and block a door.
    int badProps = 0;
    for (const Prop& p : w.props) {
        int cx, cz;
        w.worldToCell(p.pos, cx, cz);
        if (w.solid(cx, cz)) badProps++;
    }
    check(badProps == 0, "no prop is buried inside a wall");

    // Flow field must lead from anywhere reachable back to the target.
    w.computeFlow(psx, psz);
    int stranded = 0;
    for (int z = 0; z < w.H; z++)
        for (int x = 0; x < w.W; x++)
            if (reach[z * w.W + x] && w.flow[z * w.W + x] == World::FLOW_INF) stranded++;
    check(stranded == 0, "flow field covers every reachable cell");

    // Descending the flow field from the far corner must terminate at the target.
    {
        int cx = w.exitCX, cz = w.exitCZ, steps = 0;
        while (!(cx == psx && cz == psz) && steps < w.W * w.H) {
            int nx = cx, nz = cz;
            if (!w.flowStep(cx, cz, nx, nz)) break;
            cx = nx; cz = nz; steps++;
        }
        check(cx == psx && cz == psz, "flow descent from exit reaches the spawn");
    }

    // Mesh sanity: 16-bit indices mean a single mesh cannot exceed 65535 verts.
    Mesh m;
    w.buildMesh(m);
    std::printf("  world mesh: %d verts, %d tris\n", (int)m.verts.size(), (int)m.triCount());
    check(!m.verts.empty() && !m.idx.empty(), "world mesh is non-empty");
    check(m.verts.size() <= 65535, "world mesh fits in 16-bit indices");
    bool badIdx = false, badVert = false;
    for (uint16_t i : m.idx) if (i >= m.verts.size()) badIdx = true;
    for (const Vertex& v : m.verts) {
        if (!std::isfinite(v.px) || !std::isfinite(v.py) || !std::isfinite(v.pz)) badVert = true;
        float nl = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
        if (!(nl > 0.5f && nl < 1.5f)) badVert = true;
    }
    check(!badIdx, "no index points past the vertex array");
    check(!badVert, "all vertices finite with unit-length normals");

    // Line of sight must be symmetric and must not see through solid rock.
    {
        Rng r(0x5EED + ci);
        int asym = 0, throughWall = 0, trials = 0;
        for (int i = 0; i < 4000; i++) {
            int ax = r.rangei(1, w.W - 1), az = r.rangei(1, w.H - 1);
            int bx = r.rangei(1, w.W - 1), bz = r.rangei(1, w.H - 1);
            if (w.solid(ax, az) || w.solid(bx, bz)) continue;
            trials++;
            vec3 a = w.cellCenter(ax, az) + vec3(0, 1.5f, 0);
            vec3 b = w.cellCenter(bx, bz) + vec3(0, 1.5f, 0);
            bool ab = w.lineOfSight(a, b), ba = w.lineOfSight(b, a);
            if (ab != ba) asym++;
            // Two cells on opposite sides of a thick wall band must not see
            // each other; approximate by checking the midpoint cell.
            if (ab) {
                int mx = (ax + bx) / 2, mz = (az + bz) / 2;
                if (w.solid(mx, mz) && (std::abs(ax - bx) + std::abs(az - bz)) > 3) throughWall++;
            }
        }
        std::printf("  line-of-sight: %d trials, %d asymmetric, %d suspicious\n",
                    trials, asym, throughWall);
        check(trials > 500, "line-of-sight test had enough samples");
        check(asym * 200 < trials, "line of sight is essentially symmetric");
        check(throughWall == 0, "line of sight never crosses a solid midpoint");
    }
}

// ---------------------------------------------------------------------------

static void testSimulation(int ci) {
    const ChapterSpec& spec = chapterSpec(ci);
    std::printf("\n[sim %d] %s - 240s headless playthrough\n", ci + 1, spec.title);

    World w;
    w.generate(spec);
    AudioEngine audio;
    audio.start();

    Player p;
    p.reset(w.playerStart);
    Stalker s;
    s.reset(w, p.pos, spec);

    check(length(s.pos - p.pos) > 8.0f, "stalker spawns far from the player");

    Bot bot(0xA11CE + ci);
    const float dt = 1.0f / 60.0f;
    int caught = 0, escapes = 0;
    int stateSeen[8] = {0};
    float minDist = 1e9f;
    int insideWall = 0, nonFinite = 0;

    for (int frame = 0; frame < 60 * 240; frame++) {
        bot.drive(w, p);
        p.sprintHeld = (frame / 300) % 4 == 0;
        p.crouchHeld = (frame / 300) % 4 == 2;

        float cd = s.distanceTo(p.pos);
        p.update(dt, w, audio, cd, false);

        vec3 pf = p.forward();
        pf.y = 0.0f;
        pf = normalize(pf);
        if (s.update(dt, w, p.pos, pf, p.noise, p.torchOn, audio)) {
            caught++;
            // Respawn the pair and keep going so one death does not end the run.
            p.reset(w.playerStart);
            s.reset(w, p.pos, spec);
            bot.tx = -1;
            continue;
        }

        stateSeen[s.state]++;
        minDist = std::min(minDist, cd);

        if (!finite3(p.pos) || !finite3(s.pos) || !std::isfinite(p.yaw) || !std::isfinite(s.yaw))
            nonFinite++;

        int cx, cz;
        w.worldToCell(p.pos, cx, cz);
        if (w.solid(cx, cz)) insideWall++;
        w.worldToCell(s.pos, cx, cz);
        if (w.solid(cx, cz)) insideWall++;

        // Drain the audio engine at roughly the right rate so voices retire.
        if (frame % 8 == 0) audio.enqueueNext();
    }

    std::printf("  caught %d times, closest approach %.2fm\n", caught, minDist);
    std::printf("  state histogram: wander=%d investigate=%d stalk=%d hunt=%d search=%d\n",
                stateSeen[ST_WANDER], stateSeen[ST_INVESTIGATE], stateSeen[ST_STALK],
                stateSeen[ST_HUNT], stateSeen[ST_SEARCH]);

    check(nonFinite == 0, "no NaN or infinity in any position or angle");
    check(insideWall == 0, "neither the player nor the stalker ever ends up inside a wall");
    check(stateSeen[ST_WANDER] > 0, "stalker patrols");
    check(stateSeen[ST_HUNT] > 0, "stalker commits to a hunt at least once");
    check(caught > 0, "stalker is capable of catching a careless player");
    check(p.stamina >= 0.0f && p.stamina <= 1.0f, "stamina stays in range");
    check(p.battery >= 0.0f && p.battery <= 1.0f, "battery stays in range");
    (void)escapes;
    audio.stop();
}

// A player who crouch-walks should stay undetected far longer than one who
// sprints everywhere. Death count alone is a bad measure of this: the quiet
// player also moves slower, so they spend longer in the level and eat more
// random encounters, which masks the effect. What we actually care about is
// how quickly and how often the stalker gets onto them at all.
static void testStealthMatters() {
    std::printf("\n[stealth] does staying quiet actually help?\n");
    const ChapterSpec& spec = chapterSpec(1);

    struct Result { int pursuitFrames = 0; int totalFrames = 0; float firstHunt = -1.0f; };

    auto run = [&](bool sprintAlways, uint32_t seed) {
        World w;
        w.generate(spec);
        AudioEngine audio;
        audio.start();
        Player p;
        p.reset(w.playerStart);
        Stalker s;
        s.reset(w, p.pos, spec);
        Bot bot(seed);
        const float dt = 1.0f / 60.0f;
        Result res;
        for (int frame = 0; frame < 60 * 300; frame++) {
            bot.drive(w, p);
            p.sprintHeld = sprintAlways;
            p.crouchHeld = !sprintAlways;
            p.update(dt, w, audio, s.distanceTo(p.pos), false);
            vec3 pf = p.forward();
            pf.y = 0;
            pf = normalize(pf);
            if (s.update(dt, w, p.pos, pf, p.noise, false, audio)) {
                p.reset(w.playerStart);
                s.reset(w, p.pos, spec);
                bot.tx = -1;
                continue;
            }
            res.totalFrames++;
            if (s.state == ST_HUNT || s.state == ST_STALK || s.state == ST_INVESTIGATE) {
                res.pursuitFrames++;
                if (res.firstHunt < 0.0f) res.firstHunt = frame * dt;
            }
            if (frame % 8 == 0) audio.enqueueNext();
        }
        audio.stop();
        return res;
    };

    int loudPursuit = 0, quietPursuit = 0, loudTotal = 0, quietTotal = 0;
    float loudFirst = 0.0f, quietFirst = 0.0f;
    const int seeds = 6;
    for (uint32_t seed = 1; seed <= (uint32_t)seeds; seed++) {
        Result l = run(true, 0x1000 + seed);
        Result q = run(false, 0x1000 + seed);
        loudPursuit += l.pursuitFrames;   loudTotal += l.totalFrames;
        quietPursuit += q.pursuitFrames;  quietTotal += q.totalFrames;
        loudFirst += (l.firstHunt < 0.0f) ? 300.0f : l.firstHunt;
        quietFirst += (q.firstHunt < 0.0f) ? 300.0f : q.firstHunt;
    }
    float loudPct = 100.0f * loudPursuit / std::max(1, loudTotal);
    float quietPct = 100.0f * quietPursuit / std::max(1, quietTotal);
    std::printf("  sprinting: pursued %.1f%% of the time, first detected at %.0fs\n",
                loudPct, loudFirst / seeds);
    std::printf("  crouching: pursued %.1f%% of the time, first detected at %.0fs\n",
                quietPct, quietFirst / seeds);

    check(loudPct > quietPct * 1.4f, "sprinting keeps the stalker on you far more of the time");
    check(loudFirst < quietFirst, "sprinting gets you found sooner");
    check(quietPct > 0.5f, "crouching is not a total invisibility cloak");
}

// ---------------------------------------------------------------------------

static void testAudio() {
    std::printf("\n[audio] procedural mixer\n");
    AudioEngine a;
    a.start();
    a.setListener(vec3(0, 1.6f, 0), vec3(0, 0, 1));
    a.setTension(0.8f);
    a.setHeartRate(140.0f);
    a.setBreath(0.7f);

    const int frames = AudioEngine::BLOCK_FRAMES;
    std::vector<int16_t> buf(frames * 2);

    double peak = 0.0, energy = 0.0;
    long samples = 0;
    int nonFinite = 0;

    // Fire every sound in the game, several at once, and make sure the mix
    // stays inside the rails.
    for (int block = 0; block < 400; block++) {
        if (block % 3 == 0) {
            for (int id = 0; id < SND_COUNT; id++) {
                a.post((SoundId)id, vec3((float)(id % 7) - 3.0f, 1.0f, (float)(id % 5)), 1.0f, 1.0f);
            }
        }
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

    check(nonFinite == 0, "mixer never emits a non-finite sample");
    check(peak > 0.05, "mixer actually produces audible signal");
    check(peak <= 1.0, "mixer output stays within full scale");
    check(rms > 0.001 && rms < 0.5, "mixer level is sane, neither silent nor a wall of noise");

    // Silence check: with no events and no tension, output should settle near
    // zero rather than drift or self-oscillate in the reverb tank.
    AudioEngine b;
    b.start();
    b.setTension(0.0f);
    b.setHeartRate(50.0f);
    b.setBreath(0.0f);
    for (int block = 0; block < 200; block++) b.renderBlock(buf.data(), frames);
    double quietPeak = 0.0;
    for (int block = 0; block < 20; block++) {
        b.renderBlock(buf.data(), frames);
        for (int i = 0; i < frames * 2; i++) quietPeak = std::max(quietPeak, std::fabs(buf[i] / 32768.0));
    }
    std::printf("  idle peak %.4f\n", quietPeak);
    check(quietPeak < 0.35, "reverb tank does not run away when nothing is playing");
    b.stop();
    a.stop();
}

static void testCollision() {
    std::printf("\n[collision] wall resolution\n");
    World w;
    w.generate(chapterSpec(0));

    Rng r(0xDEAD);
    int escaped = 0, trials = 0;
    for (int i = 0; i < 40000; i++) {
        int cx = r.rangei(1, w.W - 1), cz = r.rangei(1, w.H - 1);
        if (w.solid(cx, cz)) continue;
        trials++;
        vec3 from = w.cellCenter(cx, cz);
        // Try to shove the player a long way in a random direction in one step:
        // the resolver must never leave them inside geometry.
        float a = r.range(0.0f, TAU);
        vec3 to = from + vec3(std::cos(a), 0, std::sin(a)) * r.range(0.1f, 6.0f);
        vec3 out = w.resolveCollision(to, Player::RADIUS);
        int ox, oz;
        w.worldToCell(out, ox, oz);
        if (w.solid(ox, oz)) escaped++;
    }
    std::printf("  %d shove tests, %d ended inside a wall\n", trials, escaped);
    check(trials > 1000, "collision test had enough samples");
    // A single huge step can tunnel; the game moves in small steps, so we only
    // require that the resolver is overwhelmingly reliable.
    check(escaped * 500 < trials, "collision resolution keeps the player out of walls");
}

static void testStalkerModel() {
    std::printf("\n[model] creature geometry\n");
    Mesh parts[BP_COUNT];
    buildStalkerParts(parts);
    int totalV = 0, totalT = 0;
    for (int i = 0; i < BP_COUNT; i++) {
        check(!parts[i].verts.empty(), "every body part has geometry");
        check(parts[i].verts.size() <= 65535, "body part fits in 16-bit indices");
        totalV += (int)parts[i].verts.size();
        totalT += (int)parts[i].triCount();
    }
    std::printf("  %d parts, %d verts, %d tris total\n", BP_COUNT, totalV, totalT);

    // Pose it and confirm the skeleton assembles into something person-shaped:
    // head above the hips, feet near the floor, arms hanging low.
    World w;
    w.generate(chapterSpec(0));
    Stalker s;
    s.reset(w, vec3(0, 0, 0), chapterSpec(0));
    s.pos = vec3(0, 0, 0);
    s.yaw = 0.0f;
    s.buildPose(0.0f);

    vec3 head = s.parts[BP_HEAD].transformPoint(vec3(0, 0.13f, 0));
    vec3 pelvis = s.parts[BP_PELVIS].transformPoint(vec3(0, 0, 0));
    vec3 footL = s.parts[BP_SHIN_L].transformPoint(vec3(0, 0.60f, 0));
    vec3 handR = s.parts[BP_FOREARM_R].transformPoint(vec3(0, 0.64f, 0));
    std::printf("  head y=%.2f  pelvis y=%.2f  foot y=%.2f  hand y=%.2f\n",
                head.y, pelvis.y, footL.y, handR.y);

    check(head.y > pelvis.y + 0.7f, "head sits well above the pelvis");
    check(head.y > 2.0f && head.y < 2.8f, "creature is tall enough to loom over a 1.66m player");
    check(footL.y < 0.35f, "feet reach the floor");
    check(handR.y < pelvis.y, "arms hang below the waist");
    for (int i = 0; i < BP_COUNT; i++)
        check(finite3(s.parts[i].transformPoint(vec3(0, 0, 0))), "every bone transform is finite");
}

int main() {
    std::printf("=== HOLLOW SIGNAL - host test suite ===\n");

    for (int i = 0; i < CHAPTER_COUNT; i++) testChapter(i);
    testStalkerModel();
    testCollision();
    testAudio();
    for (int i = 0; i < CHAPTER_COUNT; i++) testSimulation(i);
    testStealthMatters();

    std::printf("\n=== %d checks, %d failures ===\n", gChecks, gFails);
    return gFails == 0 ? 0 : 1;
}
