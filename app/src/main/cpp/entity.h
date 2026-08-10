// entity.h - "The Hollow": the thing that hunts you.
//
// Design note: it is deliberately NOT a constant chaser. A monster that is
// always sprinting at you stops being frightening after ninety seconds. This
// one spends most of its time circling just outside your torch beam, and only
// commits when it is certain. Sound is its primary sense, which is what makes
// the sprint button a real decision instead of a free win.
#pragma once
#include "hmath.h"
#include "world.h"
#include "audio.h"

namespace hm {

enum StalkerState {
    ST_DORMANT = 0,   // not yet released into the level
    ST_WANDER,        // patrolling with no information
    ST_INVESTIGATE,   // heard something, going to look
    ST_STALK,         // knows roughly where you are, circling out of sight
    ST_HUNT,          // has eyes on you, closing
    ST_SEARCH,        // lost you, sweeping the last known position
    ST_ATTACK
};

// Skeleton parts. Each is a rigid tapered prism posed by forward kinematics -
// cheaper than skinning and, with overlapping joints, reads fine at torch range.
enum BodyPart {
    BP_PELVIS = 0, BP_TORSO, BP_HEAD,
    BP_UPPERARM_L, BP_FOREARM_L, BP_UPPERARM_R, BP_FOREARM_R,
    BP_THIGH_L, BP_SHIN_L, BP_THIGH_R, BP_SHIN_R,
    BP_COUNT
};

struct Stalker {
    vec3 pos{0, 0, 0};
    float yaw = 0.0f;
    float speed = 2.2f;
    float hearing = 12.0f;
    StalkerState state = ST_DORMANT;

    vec3 targetPos{0, 0, 0};
    vec3 lastKnownPlayer{0, 0, 0};
    float stateTimer = 0.0f;
    float repathTimer = 0.0f;
    float stepTimer = 0.0f;
    float gaitPhase = 0.0f;
    float aggression = 0.0f;     // 0..1, climbs over a chapter; drives speed
    float aggressionRate = 0.006f;
    float visibleTimer = 0.0f;   // how long it has had eyes on you
    float growlTimer = 6.0f;
    float noiseMemory = 0.0f;   // slow average of how loud you have been
    float directorTimer = 30.0f; // guaranteed next approach; see update()
    float contactTimer = 0.0f;   // seconds since it last saw or heard you
    float lungeCooldown = 0.0f;

    mat4 parts[BP_COUNT];        // world transforms, refreshed each frame
    Rng rng{0xB0BAFE77u};

    void reset(const World& w, const vec3& playerPos, const ChapterSpec& spec);
    // Returns true if it reached the player this frame (i.e. you are dead).
    bool update(float dt, World& w, const vec3& playerPos, const vec3& playerFwd,
                float playerNoise, bool playerLit, AudioEngine& audio);
    void buildPose(float t);

    bool canSee(const World& w, const vec3& playerPos) const;
    float distanceTo(const vec3& p) const { return length(vec3(p.x - pos.x, 0, p.z - pos.z)); }

private:
    void pickWanderTarget(const World& w, const vec3& playerPos);
    void moveAlongFlow(float dt, const World& w, float speedScale);
};

// Builds the eleven part meshes in local space (joint at origin, growing +Y).
void buildStalkerParts(Mesh out[BP_COUNT]);

} // namespace hm
