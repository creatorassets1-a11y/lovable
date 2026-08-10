// player.h - first-person controller, torch, stamina and fear.
#pragma once
#include "hmath.h"
#include "world.h"
#include "audio.h"

namespace hm {

struct Player {
    vec3 pos{0, 0, 0};          // feet
    float yaw = 0.0f;
    float pitch = 0.0f;

    // Input, written by the touch layer each frame.
    vec2 moveInput{0, 0};       // -1..1, y is forward
    vec2 lookInput{0, 0};       // radians this frame
    bool sprintHeld = false;
    bool crouchHeld = false;

    float stamina = 1.0f;
    float battery = 1.0f;
    bool  torchOn = true;
    float fear = 0.0f;          // 0..1, drives heartbeat, vignette, breathing
    bool  alive = true;

    float bobPhase = 0.0f;
    float bobAmount = 0.0f;
    float torchFlicker = 1.0f;
    float noise = 0.0f;         // 0..1, what the stalker can hear
    float speedNow = 0.0f;
    float shake = 0.0f;         // trauma, decays; used for camera kick

    static constexpr float EYE_STAND = 1.66f;
    static constexpr float EYE_CROUCH = 0.95f;
    static constexpr float RADIUS = 0.32f;
    float eyeHeight = EYE_STAND;

    void reset(const vec3& start);
    void update(float dt, const World& w, AudioEngine& audio, float creatureDist, bool creatureVisible);

    vec3 eye() const;
    vec3 forward() const;
    vec3 right() const;
    // Torch direction includes the flicker-driven jitter, so the beam is never
    // perfectly steady - a still beam reads as CGI, a shaky one reads as a hand.
    vec3 torchDir(float time) const;
    float heartRate() const { return 58.0f + fear * 96.0f; }
    void addShake(float amount) { shake = clampf(shake + amount, 0.0f, 1.0f); }
};

} // namespace hm
