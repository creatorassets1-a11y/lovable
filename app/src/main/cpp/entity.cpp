#include "entity.h"

namespace hm {

// ------------------------------------------------------------------ modelling

void buildStalkerParts(Mesh out[BP_COUNT]) {
    for (int i = 0; i < BP_COUNT; i++) out[i].clear();

    // Proportions are deliberately wrong: the forearms are longer than the
    // upper arms and the head sits on too long a neck. You read "person" first
    // and "not a person" about half a second later.
    out[BP_PELVIS].taperedPrism({0, 0, 0}, 0.26f, 0.155f, 0.185f, 8);

    // Torso: narrow waist flaring into a hollow, over-wide ribcage.
    out[BP_TORSO].taperedPrism({0, 0, 0}, 0.46f, 0.185f, 0.235f, 8);
    out[BP_TORSO].taperedPrism({0, 0.46f, 0}, 0.26f, 0.235f, 0.115f, 8);
    // Neck
    out[BP_TORSO].taperedPrism({0, 0.72f, 0}, 0.16f, 0.058f, 0.052f, 6);

    // Head: elongated, faceless, with a distended jaw. No eyes - nothing to
    // read an intention off, which is worse than any amount of teeth.
    out[BP_HEAD].ellipsoid({0, 0.13f, 0}, {0.105f, 0.165f, 0.125f}, 12, 10);
    out[BP_HEAD].ellipsoid({0, 0.045f, 0.055f}, {0.062f, 0.085f, 0.075f}, 10, 8);

    out[BP_UPPERARM_L].taperedPrism({0, 0, 0}, 0.50f, 0.072f, 0.056f, 6);
    out[BP_UPPERARM_R].taperedPrism({0, 0, 0}, 0.50f, 0.072f, 0.056f, 6);

    // Forearm + hand. The fingers are individual splines of geometry because
    // they are the first thing that comes round a corner.
    for (int side = 0; side < 2; side++) {
        Mesh& m = out[side == 0 ? BP_FOREARM_L : BP_FOREARM_R];
        m.taperedPrism({0, 0, 0}, 0.62f, 0.056f, 0.034f, 6);
        m.ellipsoid({0, 0.64f, 0}, {0.045f, 0.055f, 0.030f}, 8, 6);
        for (int f = 0; f < 4; f++) {
            float off = (f - 1.5f) * 0.026f;
            float len = 0.16f - std::fabs(f - 1.5f) * 0.022f;
            Mesh finger;
            finger.taperedPrism({0, 0, 0}, len, 0.011f, 0.004f, 4);
            mat4 xf = mat4::translation({off, 0.66f, 0.0f})
                    * mat4::rotationX(0.35f + f * 0.05f)
                    * mat4::rotationZ(off * 2.2f);
            m.append(finger, xf);
        }
    }

    out[BP_THIGH_L].taperedPrism({0, 0, 0}, 0.55f, 0.098f, 0.075f, 6);
    out[BP_THIGH_R].taperedPrism({0, 0, 0}, 0.55f, 0.098f, 0.075f, 6);

    for (int side = 0; side < 2; side++) {
        Mesh& m = out[side == 0 ? BP_SHIN_L : BP_SHIN_R];
        m.taperedPrism({0, 0, 0}, 0.58f, 0.075f, 0.042f, 6);
        m.box({-0.055f, 0.58f, -0.055f}, {0.055f, 0.62f, 0.135f}, 2.0f);  // foot
    }
}

// ------------------------------------------------------------------- posing

void Stalker::buildPose(float t) {
    bool hunting = (state == ST_HUNT || state == ST_ATTACK);
    float g = gaitPhase;

    const float hipHeight = 1.16f;
    float bob = std::sin(g * 2.0f) * 0.035f;
    float lean = hunting ? 0.42f : 0.14f;   // pitched forward when it commits

    mat4 root = mat4::translation({pos.x, pos.y + hipHeight + bob, pos.z})
              * mat4::rotationY(yaw)
              * mat4::rotationX(-lean * 0.35f);
    parts[BP_PELVIS] = root;

    // Spine sways out of phase with the legs, and a touch too far.
    float sway = std::sin(g) * (hunting ? 0.05f : 0.10f);
    mat4 torso = root * mat4::translation({0, 0.24f, 0})
               * mat4::rotationX(-lean * 0.65f)
               * mat4::rotationZ(sway);
    parts[BP_TORSO] = torso;

    // Head: lolls while it wanders, snaps level and locked when it hunts.
    float headRoll = hunting ? std::sin(t * 1.3f) * 0.06f : std::sin(g * 0.5f + 1.0f) * 0.38f;
    float headPitch = hunting ? -0.30f : 0.34f;
    float headYaw = hunting ? 0.0f : std::sin(t * 0.7f) * 0.45f;
    parts[BP_HEAD] = torso * mat4::translation({0, 0.86f, 0})
                   * mat4::rotationY(headYaw) * mat4::rotationX(headPitch)
                   * mat4::rotationZ(headRoll);

    float amp = hunting ? 1.05f : 0.62f;
    // Left and right are not mirror images: the right side drags. The limp is
    // the single cheapest thing that makes a walk cycle feel wrong.
    float swingL = std::sin(g) * amp;
    float swingR = std::sin(g + PI) * amp * 0.66f - 0.14f;

    auto legPose = [&](int thighPart, int shinPart, float side, float swing, float kneePhase) {
        mat4 hip = root * mat4::translation({side * 0.125f, 0.02f, 0});
        mat4 thigh = hip * mat4::rotationX(PI + swing) * mat4::rotationZ(side * 0.05f);
        parts[thighPart] = thigh;
        float knee = std::max(0.0f, -std::sin(kneePhase + 0.7f)) * (hunting ? 1.35f : 0.95f);
        parts[shinPart] = thigh * mat4::translation({0, 0.55f, 0}) * mat4::rotationX(-knee);
    };
    legPose(BP_THIGH_L, BP_SHIN_L, -1.0f, swingL, g);
    legPose(BP_THIGH_R, BP_SHIN_R, 1.0f, swingR, g + PI);

    auto armPose = [&](int upperPart, int lowerPart, float side, float swing) {
        mat4 shoulder = torso * mat4::translation({side * 0.215f, 0.66f, 0});
        // Arms hang past the knees; when hunting they trail back like oars.
        float outward = hunting ? 0.16f : 0.09f;
        mat4 upper = shoulder * mat4::rotationX(PI + swing * 0.75f)
                   * mat4::rotationZ(side * -outward);
        parts[upperPart] = upper;
        float elbow = hunting ? 0.30f + std::max(0.0f, std::sin(gaitPhase)) * 0.25f : 0.16f;
        parts[lowerPart] = upper * mat4::translation({0, 0.50f, 0}) * mat4::rotationX(elbow);
    };
    armPose(BP_UPPERARM_L, BP_FOREARM_L, -1.0f, swingR);   // arms counter the legs
    armPose(BP_UPPERARM_R, BP_FOREARM_R, 1.0f, swingL);
}

// ----------------------------------------------------------------------- AI

void Stalker::reset(const World& w, const vec3& playerPos, const ChapterSpec& spec) {
    speed = spec.stalkerSpeed;
    hearing = spec.stalkerHearing;
    aggressionRate = spec.aggressionRate;
    state = ST_DORMANT;
    stateTimer = 0.0f;
    aggression = 0.0f;
    visibleTimer = 0.0f;
    gaitPhase = 0.0f;
    lungeCooldown = 0.0f;
    noiseMemory = 0.0f;
    contactTimer = 0.0f;
    directorTimer = rng.range(28.0f, 42.0f);

    // Spawn as far from the player as the map allows, so episode one does not
    // open with it in your face.
    float best = -1.0f;
    vec3 bestPos = playerPos;
    for (int z = 1; z < w.H - 1; z++) {
        for (int x = 1; x < w.W - 1; x++) {
            if (w.solid(x, z)) continue;
            vec3 c = w.cellCenter(x, z);
            float d = length(c - playerPos);
            if (d > best) { best = d; bestPos = c; }
        }
    }
    pos = bestPos;
    targetPos = bestPos;
    lastKnownPlayer = bestPos;
}

bool Stalker::canSee(const World& w, const vec3& playerPos) const {
    vec3 d = playerPos - pos;
    d.y = 0.0f;
    float dist = length(d);
    if (dist > 26.0f) return false;
    if (dist < 0.001f) return true;
    vec3 dir = d / dist;
    vec3 fwd(std::sin(yaw), 0, std::cos(yaw));
    if (dot(dir, fwd) < 0.20f && dist > 3.0f) return false;   // ~78 degree half-cone
    return w.lineOfSight(pos + vec3(0, 1.5f, 0), playerPos + vec3(0, 1.4f, 0));
}

void Stalker::pickWanderTarget(const World& w, const vec3& playerPos) {
    // Patrol is only partly random. As aggression builds over a chapter it
    // increasingly picks somewhere in the player's half of the map. Purely
    // random patrol looks reasonable in a debugger and is disastrous in play:
    // on the larger maps it can spend an entire episode in a wing you never
    // walk into, and a monster you never encounter is not frightening.
    // How strongly it converges depends on how much noise you have been
    // making. This is what gives crouching a point: a quiet player does not
    // just avoid the immediate hearing check, they stop the director steering
    // the patrol onto them in the first place.
    bool biased = rng.f01() < (0.18f + noiseMemory * 0.50f + aggression * 0.30f);
    for (int tries = 0; tries < 60; tries++) {
        int x, z;
        if (biased) {
            float a = rng.range(0.0f, TAU);
            float r = rng.range(7.0f, 22.0f);
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

void Stalker::moveAlongFlow(float dt, const World& w, float speedScale) {
    int cx, cz;
    w.worldToCell(pos, cx, cz);
    int nx = cx, nz = cz;

    vec3 goal;
    if (w.flowStep(cx, cz, nx, nz)) {
        goal = w.cellCenter(nx, nz);
    } else {
        goal = targetPos;   // already in the target cell, or no path: go direct
    }

    vec3 d = goal - pos;
    d.y = 0.0f;
    float len = length(d);
    if (len > 0.01f) {
        vec3 dir = d / len;
        float want = std::atan2(dir.x, dir.z);
        // Turn rate is capped, so it has to slow into corners. Watching it
        // over-run a junction and correct is part of the tell.
        yaw = angleTowards(yaw, want, dt * 4.5f);
        vec3 move(std::sin(yaw), 0, std::cos(yaw));
        float v = speed * speedScale;
        // Substep so a hitched frame never carries it further than its own
        // radius in one go, which is how things end up inside walls.
        const float kRadius = 0.42f;
        float travel = v * dt;
        int steps = 1 + (int)(travel / (kRadius * 0.5f));
        if (steps > 8) steps = 8;
        for (int i = 0; i < steps; i++)
            pos = w.resolveCollision(pos + move * (travel / steps), kRadius);
        gaitPhase += dt * (2.6f + v * 1.15f);
        if (gaitPhase > TAU) gaitPhase -= TAU;
    }
}

bool Stalker::update(float dt, World& w, const vec3& playerPos, const vec3& playerFwd,
                     float playerNoise, bool playerLit, AudioEngine& audio) {
    stateTimer += dt;
    repathTimer -= dt;
    growlTimer -= dt;
    if (lungeCooldown > 0.0f) lungeCooldown -= dt;

    // Slow decay, so a single sprint keeps drawing it in for a while after
    // you stop - panicking has a cost that outlives the panic.
    noiseMemory = lerpf(noiseMemory, playerNoise, clampf(dt * 0.30f, 0.0f, 1.0f));

    float dist = distanceTo(playerPos);
    bool sees = canSee(w, playerPos) && (playerLit || dist < 7.0f);
    if (sees) visibleTimer += dt; else visibleTimer = 0.0f;

    // Hearing. A sprinting player is audible across most of a chapter; a
    // crouch-walking one is nearly silent. This is the core tension knob.
    bool hears = (playerNoise > 0.02f) && (dist < hearing * playerNoise);

    // Time since it last had any real evidence of you. Successful hiding has
    // to be rewarded or the stealth systems are decorative - and a monster
    // that never disengages flattens the whole chapter into one long chase
    // with no quiet stretches to be ruined.
    if (sees || hears) contactTimer = 0.0f;
    else contactTimer += dt;

    // Aggression ramps the longer a chapter runs, so the endgame is tighter.
    aggression = clampf(aggression + dt * aggressionRate, 0.0f, 1.0f);

    // Director clock. Patrolling on its own is luck-based: on the larger maps
    // a purely emergent stalker can spend four minutes in a wing you never
    // enter, and an encounter that never happens is not tension, it is an
    // empty level. This clock guarantees it comes looking eventually. It runs
    // faster the louder you have been and the further into the chapter you
    // are, so the guarantee is also the stealth reward: keep quiet and the
    // scheduled visit is a minute away, sprint and it is ten seconds away.
    directorTimer -= dt * (0.60f + aggression * 1.20f + noiseMemory * 1.40f);
    float speedScale = 1.0f + aggression * 0.30f;

    switch (state) {
    case ST_DORMANT:
        if (stateTimer > 6.0f) { state = ST_WANDER; stateTimer = 0.0f; pickWanderTarget(w, playerPos); }
        break;

    case ST_WANDER:
        speedScale *= 0.62f;
        if (directorTimer <= 0.0f) {
            // Not "it heard you" - it simply decides to sweep where you are.
            // It arrives at your position, not at you, so staying still and
            // quiet can still let it walk past into a STALK.
            lastKnownPlayer = playerPos;
            targetPos = playerPos;
            state = ST_INVESTIGATE;
            stateTimer = 0.0f;
            repathTimer = 0.0f;
            directorTimer = rng.range(32.0f, 55.0f);
            break;
        }
        if (sees && visibleTimer > 0.35f) {
            state = ST_HUNT; stateTimer = 0.0f;
            audio.post(SND_SCREECH, pos, 0.9f, 1.0f);
        } else if (hears) {
            lastKnownPlayer = playerPos;
            targetPos = playerPos;
            state = ST_INVESTIGATE; stateTimer = 0.0f;
            repathTimer = 0.0f;
        } else if (length(targetPos - pos) < 1.4f || stateTimer > 12.0f) {
            pickWanderTarget(w, playerPos);
            stateTimer = 0.0f;
            repathTimer = 0.0f;
        }
        break;

    case ST_INVESTIGATE:
        speedScale *= 0.88f;
        if (sees && visibleTimer > 0.2f) {
            state = ST_HUNT; stateTimer = 0.0f;
            audio.post(SND_SCREECH, pos, 0.95f, 1.05f);
        } else if (hears) {
            lastKnownPlayer = playerPos;
            targetPos = playerPos;
            if (repathTimer <= 0.0f) repathTimer = 0.0f;
        } else if (length(targetPos - pos) < 1.6f || stateTimer > 14.0f) {
            // Arrived and found nothing. Now it knows you are close, and starts
            // circling rather than giving up: this is where the dread lives.
            state = ST_STALK; stateTimer = 0.0f;
        }
        break;

    case ST_STALK: {
        speedScale *= 0.80f;
        if (sees && visibleTimer > 0.25f) {
            state = ST_HUNT; stateTimer = 0.0f;
            audio.post(SND_SCREECH, pos, 1.0f, 0.98f);
            break;
        }
        if (hears && dist < hearing * playerNoise * 0.7f) {
            state = ST_HUNT; stateTimer = 0.0f;
            break;
        }
        // Re-pick a holding position on a ring around the player, preferring
        // cells the player cannot currently see.
        if (stateTimer > 3.0f || length(targetPos - pos) < 1.5f) {
            stateTimer = 0.0f;
            repathTimer = 0.0f;
            vec3 best = targetPos;
            float bestScore = -1e9f;
            for (int i = 0; i < 24; i++) {
                float a = rng.range(0.0f, TAU);
                float r = rng.range(7.0f, 13.0f);
                vec3 c = playerPos + vec3(std::cos(a) * r, 0, std::sin(a) * r);
                int cx, cz;
                w.worldToCell(c, cx, cz);
                if (!w.inBounds(cx, cz) || w.solid(cx, cz)) continue;
                vec3 cc = w.cellCenter(cx, cz);
                bool visible = w.lineOfSight(playerPos + vec3(0, 1.6f, 0), cc + vec3(0, 1.6f, 0));
                vec3 toC = normalize(cc - playerPos);
                float behind = -dot(toC, playerFwd);        // prefers your blind side
                float score = (visible ? -6.0f : 3.0f) + behind * 4.0f - std::fabs(r - 10.0f);
                if (score > bestScore) { bestScore = score; best = cc; }
            }
            targetPos = best;
        }
        if (growlTimer <= 0.0f) {
            audio.post(SND_GROWL, pos, 0.55f, rng.range(0.9f, 1.1f));
            growlTimer = rng.range(7.0f, 15.0f);
        }
        // You stayed quiet long enough. It gives up and goes back to patrol,
        // and the director clock starts the countdown to its next visit.
        if (contactTimer > 28.0f && noiseMemory < 0.16f) {
            state = ST_WANDER;
            stateTimer = 0.0f;
            directorTimer = std::max(directorTimer, rng.range(20.0f, 34.0f));
            pickWanderTarget(w, playerPos);
            repathTimer = 0.0f;
            break;
        }
        // It will not circle forever. Eventually it just comes.
        if (aggression > 0.45f && rng.f01() < dt * 0.09f) {
            state = ST_HUNT; stateTimer = 0.0f;
            audio.post(SND_SCREECH, pos, 0.85f, 1.1f);
        }
        break;
    }

    case ST_HUNT: {
        speedScale *= 1.0f;
        targetPos = playerPos;
        lastKnownPlayer = playerPos;
        if (dist < 1.25f && lungeCooldown <= 0.0f) {
            state = ST_ATTACK;
            stateTimer = 0.0f;
            return true;
        }
        if (!sees && stateTimer > 4.0f) {
            state = ST_SEARCH;
            stateTimer = 0.0f;
            targetPos = lastKnownPlayer;
        }
        if (sees) stateTimer = 0.0f;
        if (growlTimer <= 0.0f) {
            audio.post(SND_GROWL, pos, 0.8f, 1.2f);
            growlTimer = rng.range(2.5f, 5.0f);
        }
        break;
    }

    case ST_SEARCH:
        speedScale *= 0.85f;
        if (sees && visibleTimer > 0.2f) { state = ST_HUNT; stateTimer = 0.0f; }
        else if (hears) { targetPos = playerPos; lastKnownPlayer = playerPos; repathTimer = 0.0f; }
        else if (length(targetPos - pos) < 1.5f) {
            // Sweep outward from where it lost you.
            float a = rng.range(0.0f, TAU);
            float r = rng.range(3.0f, 8.0f);
            vec3 c = lastKnownPlayer + vec3(std::cos(a) * r, 0, std::sin(a) * r);
            int cx, cz;
            w.worldToCell(c, cx, cz);
            if (w.inBounds(cx, cz) && !w.solid(cx, cz)) targetPos = w.cellCenter(cx, cz);
            repathTimer = 0.0f;
        }
        if (stateTimer > 16.0f) { state = ST_STALK; stateTimer = 0.0f; }
        break;

    case ST_ATTACK:
        return true;
    }

    if (state == ST_DORMANT) { buildPose(stateTimer); return false; }

    // Repath on a timer rather than every frame: BFS over the whole grid is
    // cheap but not free, and stale-by-300ms paths are indistinguishable.
    if (repathTimer <= 0.0f) {
        int tx, tz;
        w.worldToCell(targetPos, tx, tz);
        if (w.inBounds(tx, tz) && !w.solid(tx, tz)) w.computeFlow(tx, tz);
        repathTimer = (state == ST_HUNT) ? 0.30f : 0.75f;
    }

    float before = gaitPhase;
    moveAlongFlow(dt, w, speedScale);

    // Footsteps fire off the gait cycle, so what you hear matches what it does.
    if (std::floor(before / PI) != std::floor(gaitPhase / PI)) {
        float g = (state == ST_HUNT) ? 1.0f : 0.62f;
        audio.post(SND_CREATURE_STEP, pos, g, (state == ST_HUNT) ? 1.12f : 0.94f);
    }

    buildPose(stateTimer);
    return false;
}

} // namespace hm
