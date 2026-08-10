#include "actors.h"
#include "matids.h"

namespace hm {

// --------------------------------------------------------------------- rigs

static const Rig kRigs[AK_KIND_COUNT] = {
    // Stalker: human enough to read as a person for about half a second.
    // Forearms longer than upper arms, arms past the knees, too long a neck,
    // and no face at all - nothing to read an intention off.
    {1.16f, 0.26f, 0.72f, 0.16f, 0.115f, 0.175f, 0.215f, 0.50f, 0.62f, 0.55f, 0.58f, 1.00f, 0.16f, MAT_FLESH, false},
    // Crawler: hips low, limbs short and thick, skull enormous relative to it.
    {0.62f, 0.22f, 0.52f, 0.10f, 0.135f, 0.130f, 0.245f, 0.34f, 0.40f, 0.32f, 0.34f, 1.35f, 0.12f, MAT_FLESH, false},
    // Watcher: barely a body. Almost all torso and head, vestigial legs.
    {1.34f, 0.20f, 0.98f, 0.34f, 0.130f, 0.230f, 0.180f, 0.42f, 0.72f, 0.64f, 0.68f, 0.78f, 0.22f, MAT_FLESH, false},
    // Survivor: ordinary human proportions. The contrast is the point.
    {0.94f, 0.24f, 0.62f, 0.11f, 0.100f, 0.125f, 0.195f, 0.30f, 0.28f, 0.44f, 0.44f, 1.05f, 0.09f, MAT_CLOTH, true},
};

const Rig& rigFor(ActorKind k) { return kRigs[k < AK_KIND_COUNT ? k : 0]; }

static const MonsterSpec kSpecs[AK_KIND_COUNT] = {
    // speed hearing sight aggr   attack
    {  2.55f,  16.0f, 26.0f, 0.010f, 1.30f},   // stalker
    {  4.10f,   7.0f, 13.0f, 0.020f, 1.15f},   // crawler: fast, nearly deaf
    {  0.00f,  22.0f, 30.0f, 0.004f, 0.00f},   // watcher: rooted, all senses
    {  0.00f,   0.0f,  0.0f, 0.0f,   0.00f},
};
const MonsterSpec& monsterSpec(ActorKind k) { return kSpecs[k < AK_KIND_COUNT ? k : 0]; }

// ----------------------------------------------------------------- modelling

void buildActorParts(ActorKind kind, Mesh out[BP_COUNT]) {
    const Rig& r = rigFor(kind);
    for (int i = 0; i < BP_COUNT; i++) out[i].clear();
    float g = r.girth;
    float mat = (float)r.material;
    float skin = (float)(r.clothed ? MAT_SKIN : r.material);

    out[BP_PELVIS].taperedPrism({0, 0, 0}, r.pelvisLen, 0.155f * g, 0.185f * g, 8, mat);

    // Torso: narrow waist flaring into an over-wide ribcage, then a neck.
    float t1 = r.torsoLen * 0.64f, t2 = r.torsoLen - t1;
    out[BP_TORSO].taperedPrism({0, 0, 0}, t1, 0.185f * g, 0.235f * g, 8, mat);
    out[BP_TORSO].taperedPrism({0, t1, 0}, t2, 0.235f * g, 0.115f * g, 8, mat);
    out[BP_TORSO].taperedPrism({0, r.torsoLen, 0}, r.neckLen, 0.058f * g, 0.052f * g, 6, skin);

    out[BP_HEAD].ellipsoid({0, r.headLen * 0.75f, 0},
                           {r.headR, r.headLen, r.headR * 1.09f}, 12, 10, skin);
    // Distended jaw pushed forward: the only facial feature any of them have.
    out[BP_HEAD].ellipsoid({0, r.headLen * 0.26f, r.headR * 0.48f},
                           {r.headR * 0.59f, r.headLen * 0.52f, r.headR * 0.65f}, 10, 8, skin);

    out[BP_UPPERARM_L].taperedPrism({0, 0, 0}, r.upperArm, 0.072f * g, 0.056f * g, 6, mat);
    out[BP_UPPERARM_R].taperedPrism({0, 0, 0}, r.upperArm, 0.072f * g, 0.056f * g, 6, mat);

    for (int side = 0; side < 2; side++) {
        Mesh& m = out[side == 0 ? BP_FOREARM_L : BP_FOREARM_R];
        m.taperedPrism({0, 0, 0}, r.foreArm, 0.056f * g, 0.034f * g, 6, mat);
        m.ellipsoid({0, r.foreArm + 0.02f, 0},
                    {0.045f * g, 0.055f * g, 0.030f * g}, 8, 6, skin);
        // Fingers are individual geometry because they are the first thing
        // that comes round a corner.
        for (int f = 0; f < 4; f++) {
            float off = (f - 1.5f) * 0.026f * g;
            float len = r.fingerLen - std::fabs(f - 1.5f) * 0.022f;
            Mesh finger;
            finger.taperedPrism({0, 0, 0}, len, 0.011f * g, 0.004f * g, 4, skin);
            m.append(finger, mat4::translation({off, r.foreArm + 0.04f, 0.0f})
                           * mat4::rotationX(0.35f + f * 0.05f)
                           * mat4::rotationZ(off * 2.2f));
        }
    }

    out[BP_THIGH_L].taperedPrism({0, 0, 0}, r.thigh, 0.098f * g, 0.075f * g, 6, mat);
    out[BP_THIGH_R].taperedPrism({0, 0, 0}, r.thigh, 0.098f * g, 0.075f * g, 6, mat);

    for (int side = 0; side < 2; side++) {
        Mesh& m = out[side == 0 ? BP_SHIN_L : BP_SHIN_R];
        m.taperedPrism({0, 0, 0}, r.shin, 0.075f * g, 0.042f * g, 6, mat);
        m.box({-0.055f * g, r.shin, -0.055f * g},
              {0.055f * g, r.shin + 0.04f, 0.135f * g},
              r.clothed ? (float)MAT_RUSTMETAL : mat, 2.0f);
    }
}

// -------------------------------------------------------------------- posing

void poseHumanoid(mat4 out[BP_COUNT], const Rig& r, const vec3& pos, float yaw,
                  float g, float t, float lean, float amp,
                  bool limp, bool crawl, float headYawOverride) {
    float bob = std::sin(g * 2.0f) * 0.035f;
    float hip = crawl ? r.hipHeight * 0.86f : r.hipHeight;

    mat4 root = mat4::translation({pos.x, pos.y + hip + bob, pos.z})
              * mat4::rotationY(yaw)
              * mat4::rotationX(-lean * 0.35f);
    out[BP_PELVIS] = root;

    float sway = std::sin(g) * (amp > 0.8f ? 0.05f : 0.10f);
    mat4 torso = root * mat4::translation({0, r.pelvisLen * 0.92f, 0})
               * mat4::rotationX(-lean * 0.65f)
               * mat4::rotationZ(sway);
    out[BP_TORSO] = torso;

    float headRoll = (amp > 0.8f) ? std::sin(t * 1.3f) * 0.06f
                                  : std::sin(g * 0.5f + 1.0f) * 0.38f;
    float headPitch = crawl ? -0.55f : ((amp > 0.8f) ? -0.30f : 0.34f);
    float headYaw = (headYawOverride > -9.0f) ? headYawOverride
                                              : std::sin(t * 0.7f) * 0.45f;
    out[BP_HEAD] = torso * mat4::translation({0, r.torsoLen + r.neckLen, 0})
                 * mat4::rotationY(headYaw) * mat4::rotationX(headPitch)
                 * mat4::rotationZ(headRoll);

    // Left and right are not mirror images when `limp` is set: the right side
    // drags. It is the cheapest thing that makes a walk cycle feel wrong.
    float swingL = std::sin(g) * amp;
    float swingR = std::sin(g + PI) * amp * (limp ? 0.66f : 1.0f) - (limp ? 0.14f : 0.0f);

    auto legPose = [&](int thighPart, int shinPart, float side, float swing, float kneePhase) {
        mat4 hipM = root * mat4::translation({side * 0.125f, 0.02f, 0});
        mat4 thigh = hipM * mat4::rotationX(PI + swing) * mat4::rotationZ(side * 0.05f);
        out[thighPart] = thigh;
        float knee = std::max(0.0f, -std::sin(kneePhase + 0.7f)) * (amp > 0.8f ? 1.35f : 0.95f);
        out[shinPart] = thigh * mat4::translation({0, r.thigh, 0}) * mat4::rotationX(-knee);
    };
    legPose(BP_THIGH_L, BP_SHIN_L, -1.0f, swingL, g);
    legPose(BP_THIGH_R, BP_SHIN_R, 1.0f, swingR, g + PI);

    auto armPose = [&](int upperPart, int lowerPart, float side, float swing) {
        mat4 shoulder = torso * mat4::translation({side * r.shoulderW, r.torsoLen * 0.92f, 0});
        if (crawl) {
            // Arms plant forward and down: it is walking on its hands.
            mat4 upper = shoulder * mat4::rotationX(PI - 0.85f + swing * 0.55f)
                       * mat4::rotationZ(side * -0.28f);
            out[upperPart] = upper;
            out[lowerPart] = upper * mat4::translation({0, r.upperArm, 0})
                           * mat4::rotationX(0.95f);
            return;
        }
        float outward = (amp > 0.8f) ? 0.16f : 0.09f;
        mat4 upper = shoulder * mat4::rotationX(PI + swing * 0.75f)
                   * mat4::rotationZ(side * -outward);
        out[upperPart] = upper;
        float elbow = (amp > 0.8f) ? 0.30f + std::max(0.0f, std::sin(g)) * 0.25f : 0.16f;
        out[lowerPart] = upper * mat4::translation({0, r.upperArm, 0}) * mat4::rotationX(elbow);
    };
    armPose(BP_UPPERARM_L, BP_FOREARM_L, -1.0f, swingR);   // arms counter the legs
    armPose(BP_UPPERARM_R, BP_FOREARM_R, 1.0f, swingL);
}

void Monster::buildPose(float t) {
    const Rig& r = rigFor(kind);
    bool hunting = (state == AS_HUNT || state == AS_ATTACK);
    float lean = hunting ? 0.42f : 0.14f;
    float amp = hunting ? 1.05f : 0.62f;

    if (kind == AK_CRAWLER) {
        lean = 1.05f;
        amp = hunting ? 1.35f : 0.75f;
    } else if (kind == AK_WATCHER) {
        // It does not walk. It sways, and it turns its head to track you.
        lean = 0.0f;
        amp = 0.06f;
    }
    if (stunTimer > 0.0f) {
        // Recoiled: doubled over, arms up.
        lean = 1.25f;
        amp = 0.15f;
    }
    float headYaw = -99.0f;
    if (kind == AK_WATCHER) {
        vec3 d = lastKnownPlayer - pos;
        float want = std::atan2(d.x, d.z);
        float rel = want - yaw;
        while (rel > PI) rel -= TAU;
        while (rel < -PI) rel += TAU;
        headYaw = clampf(rel, -1.5f, 1.5f);
    }
    poseHumanoid(parts, r, pos, yaw, gaitPhase, t, lean, amp,
                 kind == AK_STALKER, kind == AK_CRAWLER, headYaw);
}

void Npc::buildPose(float t) {
    const Rig& r = rigFor(AK_SURVIVOR);
    bool running = (state == AS_FLEE);
    float lean = running ? 0.38f : 0.08f;
    float amp = running ? 1.10f : (state == AS_FOLLOW ? 0.70f : 0.16f);
    if (state == AS_DOWNED) {
        // Collapsed: rotated onto its side on the ground.
        mat4 fall = mat4::translation({pos.x, pos.y + 0.22f, pos.z})
                  * mat4::rotationY(yaw) * mat4::rotationZ(1.45f);
        poseHumanoid(parts, r, vec3(0, 0, 0), 0.0f, 0.0f, t, 0.0f, 0.05f, false, false, -99.0f);
        for (int i = 0; i < BP_COUNT; i++) parts[i] = fall * parts[i];
        return;
    }
    poseHumanoid(parts, r, pos, yaw, gaitPhase, t, lean, amp, false, false, -99.0f);
}

// ------------------------------------------------------------------- monster

void Monster::spawn(const World& w, const vec3& at, ActorKind k, uint32_t seed) {
    kind = k;
    pos = at;
    pos.y = 0.0f;
    rng = Rng(seed ? seed : 1u);
    state = AS_DORMANT;
    stateTimer = 0.0f;
    aggression = 0.0f;
    visibleTimer = 0.0f;
    gaitPhase = 0.0f;
    noiseMemory = 0.0f;
    contactTimer = 0.0f;
    stunTimer = 0.0f;
    alertTimer = 0.0f;
    directorTimer = rng.range(26.0f, 44.0f);
    targetPos = at;
    lastKnownPlayer = at;
    active = true;
    yaw = rng.range(0.0f, TAU);
    (void)w;
}

void Monster::alertTo(const vec3& where) {
    // Called by a watcher's scream: everything nearby now knows roughly where
    // you are, without having sensed you itself.
    if (!active || kind == AK_WATCHER) return;
    lastKnownPlayer = where;
    targetPos = where;
    if (state == AS_WANDER || state == AS_DORMANT || state == AS_STALK) {
        state = AS_INVESTIGATE;
        stateTimer = 0.0f;
        repathTimer = 0.0f;
    }
    aggression = clampf(aggression + 0.18f, 0.0f, 1.0f);
    alertTimer = 3.0f;
}

bool Monster::canSee(const World& w, const vec3& playerPos) const {
    const MonsterSpec& sp = monsterSpec(kind);
    vec3 d = playerPos - pos;
    d.y = 0.0f;
    float dist = length(d);
    if (dist > sp.sightRange) return false;
    if (dist < 0.001f) return true;
    vec3 dir = d / dist;
    vec3 fwd(std::sin(yaw), 0, std::cos(yaw));
    // The watcher does not need to turn to see you.
    float cone = (kind == AK_WATCHER) ? -0.4f : 0.20f;
    if (dot(dir, fwd) < cone && dist > 3.0f) return false;
    return w.lineOfSight(pos + vec3(0, 1.5f, 0), playerPos + vec3(0, 1.4f, 0));
}

void Monster::pickWanderTarget(const World& w, const vec3& playerPos) {
    // Patrol is only partly random. How strongly it converges on you depends on
    // how much noise you have been making, which is what gives crouching a
    // point beyond the immediate hearing check.
    bool biased = rng.f01() < (0.18f + noiseMemory * 0.50f + aggression * 0.30f);
    for (int tries = 0; tries < 60; tries++) {
        int x, z;
        if (biased) {
            float a = rng.range(0.0f, TAU);
            float r = rng.range(8.0f, 26.0f);
            w.worldToCell(playerPos + vec3(std::cos(a) * r, 0, std::sin(a) * r), x, z);
        } else {
            x = rng.rangei(1, w.W - 1);
            z = rng.rangei(1, w.H - 1);
        }
        if (!w.inBounds(x, z) || w.solid(x, z)) continue;
        vec3 c = w.cellCenter(x, z);
        if (length(c - pos) < 6.0f) continue;
        targetPos = c;
        return;
    }
}

void Monster::moveAlongFlow(float dt, const World& w, float speedScale) {
    int cx, cz;
    w.worldToCell(pos, cx, cz);
    int nx = cx, nz = cz;
    vec3 goal = w.flowStep(cx, cz, nx, nz) ? w.cellCenter(nx, nz) : targetPos;

    vec3 d = goal - pos;
    d.y = 0.0f;
    float len = length(d);
    if (len <= 0.01f) return;

    vec3 dir = d / len;
    float want = std::atan2(dir.x, dir.z);
    // Capped turn rate, so it has to slow into corners. Watching it over-run a
    // junction and correct is part of the tell that it is not on rails.
    yaw = angleTowards(yaw, want, dt * (kind == AK_CRAWLER ? 6.5f : 4.5f));

    vec3 move(std::sin(yaw), 0, std::cos(yaw));
    float v = monsterSpec(kind).speed * speedScale;
    const float radius = (kind == AK_CRAWLER) ? 0.36f : 0.42f;
    float travel = v * dt;
    int steps = 1 + (int)(travel / (radius * 0.5f));
    if (steps > 8) steps = 8;
    for (int i = 0; i < steps; i++)
        pos = w.resolveCollision(pos + move * (travel / steps), radius);
    gaitPhase += dt * (2.6f + v * (kind == AK_CRAWLER ? 1.9f : 1.15f));
    if (gaitPhase > TAU) gaitPhase -= TAU;
}

bool Monster::update(float dt, World& w, const vec3& playerPos, const vec3& playerFwd,
                     float playerNoise, bool playerLit, AudioEngine& audio,
                     bool playerIndoors) {
    if (!active) return false;
    const MonsterSpec& sp = monsterSpec(kind);
    stateTimer += dt;
    repathTimer -= dt;
    growlTimer -= dt;
    if (alertTimer > 0.0f) alertTimer -= dt;

    if (stunTimer > 0.0f) {
        stunTimer -= dt;
        buildPose(stateTimer);
        return false;
    }

    noiseMemory = lerpf(noiseMemory, playerNoise, clampf(dt * 0.30f, 0.0f, 1.0f));

    float dist = distanceTo(playerPos);
    bool sees = canSee(w, playerPos) && (playerLit || dist < 7.0f);
    if (sees) visibleTimer += dt; else visibleTimer = 0.0f;
    bool hears = (playerNoise > 0.02f) && (dist < sp.hearing * playerNoise);

    if (sees || hears) contactTimer = 0.0f;
    else contactTimer += dt;

    aggression = clampf(aggression + dt * sp.aggressionRate, 0.0f, 1.0f);
    float speedScale = 1.0f + aggression * 0.30f;

    // ------------------------------------------------------ watcher: rooted
    if (kind == AK_WATCHER) {
        if (sees || hears) {
            lastKnownPlayer = playerPos;
            if (state != AS_HUNT) {
                state = AS_HUNT;
                stateTimer = 0.0f;
                // The scream is the whole point of this one: it does not chase,
                // it tells everything else where you are.
                audio.post(SND_WATCHER_CALL, pos, 1.0f, 1.0f);
            } else if (growlTimer <= 0.0f) {
                audio.post(SND_WATCHER_CALL, pos, 0.85f, rng.range(0.95f, 1.1f));
                growlTimer = rng.range(5.0f, 9.0f);
            }
        } else if (state == AS_HUNT && stateTimer > 6.0f) {
            state = AS_WANDER;
            stateTimer = 0.0f;
        }
        // A slow idle sway, and it always faces roughly toward you.
        vec3 d = playerPos - pos;
        if (lengthSq(d) > 0.01f)
            yaw = angleTowards(yaw, std::atan2(d.x, d.z), dt * 0.7f);
        gaitPhase += dt * 0.8f;
        buildPose(stateTimer);
        return false;
    }

    // -------------------------------------------------------- crawler + stalker
    directorTimer -= dt * (0.35f + aggression * 1.10f + noiseMemory * 2.40f);

    switch (state) {
    case AS_DORMANT:
        if (stateTimer > (kind == AK_CRAWLER ? 3.0f : 6.0f)) {
            state = AS_WANDER;
            stateTimer = 0.0f;
            pickWanderTarget(w, playerPos);
        }
        break;

    case AS_WANDER:
        speedScale *= 0.62f;
        if (directorTimer <= 0.0f) {
            // Not "it heard you": it simply decides to sweep where you are.
            // Purely emergent patrol is luck-based on a map this size, and an
            // encounter that never happens is not tension, it is an empty city.
            lastKnownPlayer = playerPos;
            targetPos = playerPos;
            state = AS_INVESTIGATE;
            stateTimer = 0.0f;
            repathTimer = 0.0f;
            directorTimer = rng.range(34.0f, 62.0f);
            break;
        }
        if (sees && visibleTimer > 0.35f) {
            state = AS_HUNT;
            stateTimer = 0.0f;
            audio.post(SND_SCREECH, pos, 0.9f, kind == AK_CRAWLER ? 1.35f : 1.0f);
        } else if (hears) {
            lastKnownPlayer = playerPos;
            targetPos = playerPos;
            state = AS_INVESTIGATE;
            stateTimer = 0.0f;
            repathTimer = 0.0f;
        } else if (length(targetPos - pos) < 1.6f || stateTimer > 12.0f) {
            pickWanderTarget(w, playerPos);
            stateTimer = 0.0f;
            repathTimer = 0.0f;
        }
        break;

    case AS_INVESTIGATE:
        speedScale *= 0.88f;
        if (sees && visibleTimer > 0.2f) {
            state = AS_HUNT;
            stateTimer = 0.0f;
            audio.post(SND_SCREECH, pos, 0.95f, kind == AK_CRAWLER ? 1.4f : 1.05f);
        } else if (hears) {
            lastKnownPlayer = playerPos;
            targetPos = playerPos;
        } else if (length(targetPos - pos) < 1.8f || stateTimer > 16.0f) {
            state = AS_STALK;
            stateTimer = 0.0f;
        }
        break;

    case AS_STALK: {
        speedScale *= 0.80f;
        if (sees && visibleTimer > 0.25f) {
            state = AS_HUNT;
            stateTimer = 0.0f;
            audio.post(SND_SCREECH, pos, 1.0f, 0.98f);
            break;
        }
        if (hears && dist < sp.hearing * playerNoise * 0.7f) {
            state = AS_HUNT;
            stateTimer = 0.0f;
            break;
        }
        // Hold a position on a ring around the player, preferring cells they
        // cannot see and their blind side. This is where the dread lives.
        if (stateTimer > 3.0f || length(targetPos - pos) < 1.8f) {
            stateTimer = 0.0f;
            repathTimer = 0.0f;
            vec3 best = targetPos;
            float bestScore = -1e9f;
            for (int i = 0; i < 24; i++) {
                float a = rng.range(0.0f, TAU);
                float rr = rng.range(8.0f, 15.0f);
                vec3 c = playerPos + vec3(std::cos(a) * rr, 0, std::sin(a) * rr);
                int cx, cz;
                w.worldToCell(c, cx, cz);
                if (!w.inBounds(cx, cz) || w.solid(cx, cz)) continue;
                vec3 cc = w.cellCenter(cx, cz);
                bool visible = w.lineOfSight(playerPos + vec3(0, 1.6f, 0), cc + vec3(0, 1.6f, 0));
                float behind = -dot(normalize(cc - playerPos), playerFwd);
                float score = (visible ? -6.0f : 3.0f) + behind * 4.0f - std::fabs(rr - 11.0f);
                if (score > bestScore) { bestScore = score; best = cc; }
            }
            targetPos = best;
        }
        if (growlTimer <= 0.0f) {
            audio.post(kind == AK_CRAWLER ? SND_CHITTER : SND_GROWL, pos,
                       0.55f, rng.range(0.9f, 1.1f));
            growlTimer = rng.range(7.0f, 15.0f);
        }
        // Stay quiet long enough and it loses interest. Without this a chapter
        // flattens into one unbroken chase with no quiet stretches to ruin.
        if (contactTimer > 28.0f && noiseMemory < 0.16f) {
            state = AS_WANDER;
            stateTimer = 0.0f;
            directorTimer = std::max(directorTimer, rng.range(20.0f, 36.0f));
            pickWanderTarget(w, playerPos);
            repathTimer = 0.0f;
            break;
        }
        if (aggression > 0.45f && rng.f01() < dt * 0.09f) {
            state = AS_HUNT;
            stateTimer = 0.0f;
            audio.post(SND_SCREECH, pos, 0.85f, 1.1f);
        }
        break;
    }

    case AS_HUNT:
        targetPos = playerPos;
        lastKnownPlayer = playerPos;
        if (dist < sp.attackRange) {
            state = AS_ATTACK;
            stateTimer = 0.0f;
            return true;
        }
        if (!sees && stateTimer > 4.0f) {
            state = AS_SEARCH;
            stateTimer = 0.0f;
            targetPos = lastKnownPlayer;
        }
        if (sees) stateTimer = 0.0f;
        if (growlTimer <= 0.0f) {
            audio.post(kind == AK_CRAWLER ? SND_CHITTER : SND_GROWL, pos, 0.8f, 1.2f);
            growlTimer = rng.range(2.5f, 5.0f);
        }
        break;

    case AS_SEARCH:
        speedScale *= 0.85f;
        if (sees && visibleTimer > 0.2f) { state = AS_HUNT; stateTimer = 0.0f; }
        else if (hears) { targetPos = playerPos; lastKnownPlayer = playerPos; repathTimer = 0.0f; }
        else if (length(targetPos - pos) < 1.8f) {
            float a = rng.range(0.0f, TAU);
            float rr = rng.range(4.0f, 10.0f);
            vec3 c = lastKnownPlayer + vec3(std::cos(a) * rr, 0, std::sin(a) * rr);
            int cx, cz;
            w.worldToCell(c, cx, cz);
            if (w.inBounds(cx, cz) && !w.solid(cx, cz)) targetPos = w.cellCenter(cx, cz);
            repathTimer = 0.0f;
        }
        if (stateTimer > 16.0f) { state = AS_STALK; stateTimer = 0.0f; }
        break;

    case AS_ATTACK:
        return true;

    default:
        break;
    }

    if (state == AS_DORMANT) { buildPose(stateTimer); return false; }

    // Repath on a timer: BFS over the whole grid is cheap but not free, and a
    // path that is 300ms stale is indistinguishable from a fresh one.
    if (repathTimer <= 0.0f) {
        int tx, tz;
        w.worldToCell(targetPos, tx, tz);
        if (w.inBounds(tx, tz) && !w.solid(tx, tz)) w.computeFlow(tx, tz);
        repathTimer = (state == AS_HUNT) ? 0.30f : 0.80f;
    }

    float before = gaitPhase;
    moveAlongFlow(dt, w, speedScale);

    // Footsteps fire off the gait cycle so what you hear matches what it does.
    if (std::floor(before / PI) != std::floor(gaitPhase / PI)) {
        float g = (state == AS_HUNT) ? 1.0f : 0.62f;
        if (playerIndoors) g *= 1.15f;   // interiors are louder and closer
        audio.post(SND_CREATURE_STEP, pos, g,
                   (kind == AK_CRAWLER ? 1.5f : 1.0f) * ((state == AS_HUNT) ? 1.12f : 0.94f));
    }

    buildPose(stateTimer);
    return false;
}

// ----------------------------------------------------------------------- NPC

void Npc::spawn(const vec3& at, int id_, uint32_t seed) {
    pos = at;
    pos.y = 0.0f;
    homePos = at;
    targetPos = at;
    id = id_;
    rng = Rng(seed ? seed : 3u);
    voiceIndex = rng.rangei(0, 2);
    state = AS_IDLE;
    alive = true;
    rescued = false;
    active = true;
    yaw = rng.range(0.0f, TAU);
    speakTimer = rng.range(3.0f, 12.0f);
    panic = 0.0f;
}

void Npc::update(float dt, World& w, const vec3& playerPos,
                 const vec3& nearestMonsterPos, float nearestMonsterDist,
                 AudioEngine& audio, bool following) {
    if (!active || !alive) return;
    stateTimer += dt;
    repathTimer -= dt;
    speakTimer -= dt;

    if (state == AS_DOWNED) { buildPose(stateTimer); return; }

    // Panic rises with proximity to a monster and decays slowly.
    float threat = clampf(1.0f - nearestMonsterDist / 18.0f, 0.0f, 1.0f);
    panic = clampf(panic + (threat - panic) * clampf(dt * (threat > panic ? 1.6f : 0.25f), 0.0f, 1.0f),
                   0.0f, 1.0f);

    float speed = 0.0f;
    if (panic > 0.55f) {
        // Flee directly away from the monster, hugging whatever is walkable.
        if (state != AS_FLEE) { state = AS_FLEE; stateTimer = 0.0f; }
        vec3 away = pos - nearestMonsterPos;
        away.y = 0.0f;
        if (lengthSq(away) > 0.01f) {
            away = normalize(away);
            targetPos = pos + away * 6.0f;
            yaw = angleTowards(yaw, std::atan2(away.x, away.z), dt * 5.0f);
        }
        speed = 3.6f;
        if (speakTimer <= 0.0f) {
            audio.post(SND_NPC_RUN, pos, 0.9f, 1.0f);
            speakTimer = rng.range(4.0f, 9.0f);
        }
    } else if (following) {
        if (state != AS_FOLLOW) { state = AS_FOLLOW; stateTimer = 0.0f; }
        float d = distanceTo(playerPos);
        if (d > 2.6f) {
            targetPos = playerPos;
            speed = (d > 8.0f) ? 3.4f : 2.3f;
            if (repathTimer <= 0.0f) {
                int tx, tz;
                w.worldToCell(targetPos, tx, tz);
                if (w.inBounds(tx, tz) && !w.solid(tx, tz)) w.computeFlow(tx, tz);
                repathTimer = 0.5f;
            }
            int cx, cz, nx, nz;
            w.worldToCell(pos, cx, cz);
            vec3 goal = w.flowStep(cx, cz, nx, nz) ? w.cellCenter(nx, nz) : targetPos;
            vec3 dir = goal - pos;
            dir.y = 0.0f;
            if (lengthSq(dir) > 0.0001f)
                yaw = angleTowards(yaw, std::atan2(dir.x, dir.z), dt * 5.0f);
        } else {
            speed = 0.0f;
            vec3 look = playerPos - pos;
            if (lengthSq(look) > 0.01f)
                yaw = angleTowards(yaw, std::atan2(look.x, look.z), dt * 3.0f);
        }
        if (speakTimer <= 0.0f) {
            audio.post(SND_NPC_FOLLOW, pos, 0.7f, 1.0f);
            speakTimer = rng.range(14.0f, 30.0f);
        }
    } else {
        if (state != AS_IDLE) { state = AS_IDLE; stateTimer = 0.0f; }
        speed = 0.0f;
        // Waiting where they were left, looking around. When the player gets
        // close they call out, which is how you find them in the dark.
        yaw += std::sin(stateTimer * 0.4f) * dt * 0.8f;
        if (speakTimer <= 0.0f) {
            float d = distanceTo(playerPos);
            audio.post(d < 22.0f ? SND_NPC_HELP : SND_NPC_SOB, pos,
                       d < 22.0f ? 0.85f : 0.5f, 1.0f);
            speakTimer = rng.range(9.0f, 20.0f);
        }
    }

    if (speed > 0.01f) {
        vec3 move(std::sin(yaw), 0, std::cos(yaw));
        float travel = speed * dt;
        int steps = 1 + (int)(travel / 0.16f);
        if (steps > 8) steps = 8;
        for (int i = 0; i < steps; i++)
            pos = w.resolveCollision(pos + move * (travel / steps), 0.32f);
        gaitPhase += dt * (2.4f + speed * 1.3f);
        if (gaitPhase > TAU) gaitPhase -= TAU;
    }

    buildPose(stateTimer);
}

} // namespace hm
