// actors.h - everything that walks around: three kinds of monster, plus the
// survivors you are sent to find.
//
// They share one skeleton and one poser. What separates a survivor from the
// thing hunting you is proportion, gait and behaviour, not a different rig.
#pragma once
#include "hmath.h"
#include "world.h"
#include "audio.h"

namespace hm {

enum BodyPart {
    BP_PELVIS = 0, BP_TORSO, BP_HEAD,
    BP_UPPERARM_L, BP_FOREARM_L, BP_UPPERARM_R, BP_FOREARM_R,
    BP_THIGH_L, BP_SHIN_L, BP_THIGH_R, BP_SHIN_R,
    BP_COUNT
};

// One rig, retargeted. Every actor mesh in the game comes out of this.
struct Rig {
    float hipHeight = 1.16f;
    float pelvisLen = 0.26f;
    float torsoLen = 0.72f;
    float neckLen = 0.16f;
    float headR = 0.115f;
    float headLen = 0.165f;
    float shoulderW = 0.215f;
    float upperArm = 0.50f;
    float foreArm = 0.62f;
    float thigh = 0.55f;
    float shin = 0.58f;
    float girth = 1.0f;      // multiplies every limb radius
    float fingerLen = 0.16f;
    int   material = 0;
    bool  clothed = false;
};

enum ActorKind {
    AK_STALKER = 0,   // tall, patient, hunts by sound
    AK_CRAWLER,       // low and fast, poor senses, comes in a rush
    AK_WATCHER,       // rooted; screams and calls the others to you
    AK_SURVIVOR,
    AK_KIND_COUNT
};

const Rig& rigFor(ActorKind k);
void buildActorParts(ActorKind kind, Mesh out[BP_COUNT]);

enum ActorState {
    AS_DORMANT = 0,
    AS_WANDER,
    AS_INVESTIGATE,
    AS_STALK,
    AS_HUNT,
    AS_SEARCH,
    AS_ATTACK,
    // survivor-only
    AS_IDLE,
    AS_FOLLOW,
    AS_FLEE,
    AS_DOWNED,
    AS_STATE_COUNT
};

struct MonsterSpec {
    float speed;
    float hearing;         // metres at full player noise
    float sightRange;
    float aggressionRate;
    float attackRange;
};
const MonsterSpec& monsterSpec(ActorKind k);

struct Monster {
    ActorKind kind = AK_STALKER;
    vec3 pos{0, 0, 0};
    float yaw = 0.0f;
    ActorState state = AS_DORMANT;
    bool active = false;

    vec3 targetPos{0, 0, 0};
    vec3 lastKnownPlayer{0, 0, 0};
    float stateTimer = 0.0f;
    float repathTimer = 0.0f;
    float gaitPhase = 0.0f;
    float aggression = 0.0f;
    float visibleTimer = 0.0f;
    float growlTimer = 6.0f;
    float noiseMemory = 0.0f;
    float directorTimer = 30.0f;
    float contactTimer = 0.0f;
    float stunTimer = 0.0f;      // flare hit: buys you seconds, never a kill
    float alertTimer = 0.0f;

    mat4 parts[BP_COUNT];
    Rng rng{0xB0BAFE77u};

    void spawn(const World& w, const vec3& at, ActorKind k, uint32_t seed);
    // Returns true if it reached the player this frame.
    bool update(float dt, World& w, const vec3& playerPos, const vec3& playerFwd,
                float playerNoise, bool playerLit, AudioEngine& audio,
                bool playerIndoors);
    void buildPose(float t);
    bool canSee(const World& w, const vec3& playerPos) const;
    float distanceTo(const vec3& p) const {
        return length(vec3(p.x - pos.x, 0, p.z - pos.z));
    }
    void alertTo(const vec3& where);
    void stun(float seconds) { stunTimer = std::max(stunTimer, seconds); }

private:
    void pickWanderTarget(const World& w, const vec3& playerPos);
    void moveAlongFlow(float dt, const World& w, float speedScale);
};

struct Npc {
    vec3 pos{0, 0, 0};
    float yaw = 0.0f;
    ActorState state = AS_IDLE;
    bool alive = true;
    bool rescued = false;
    bool active = false;
    int  voiceIndex = 0;         // which survivor voice this one uses
    int  id = 0;

    vec3 homePos{0, 0, 0};
    vec3 targetPos{0, 0, 0};
    float stateTimer = 0.0f;
    float repathTimer = 0.0f;
    float gaitPhase = 0.0f;
    float speakTimer = 0.0f;
    float panic = 0.0f;

    mat4 parts[BP_COUNT];
    Rng rng{0x51D3u};

    void spawn(const vec3& at, int id_, uint32_t seed);
    void update(float dt, World& w, const vec3& playerPos,
                const vec3& nearestMonsterPos, float nearestMonsterDist,
                AudioEngine& audio, bool following);
    void buildPose(float t);
    float distanceTo(const vec3& p) const {
        return length(vec3(p.x - pos.x, 0, p.z - pos.z));
    }
};

// Shared forward-kinematics poser. `crawl` bends the whole body forward onto
// all fours, which is what makes the crawler read as an animal.
void poseHumanoid(mat4 out[BP_COUNT], const Rig& rig, const vec3& pos, float yaw,
                  float gaitPhase, float t, float lean, float amp,
                  bool limp, bool crawl, float headYawOverride);

} // namespace hm
