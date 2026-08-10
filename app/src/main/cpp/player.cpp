#include "player.h"

namespace hm {

void Player::reset(const vec3& start) {
    pos = start;
    pos.y = 0.0f;
    yaw = 0.0f;
    pitch = 0.0f;
    stamina = 1.0f;
    battery = 1.0f;
    torchOn = true;
    fear = 0.0f;
    alive = true;
    bobPhase = 0.0f;
    bobAmount = 0.0f;
    shake = 0.0f;
    eyeHeight = EYE_STAND;
    moveInput = vec2(0, 0);
    lookInput = vec2(0, 0);
}

vec3 Player::forward() const {
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}

vec3 Player::right() const {
    // cross(forward, up), which is the vector mat4::lookAt puts along screen-X
    // and the vector the audio mixer pans against. This used to return the
    // negation of that, so strafing left moved you right - self-consistent,
    // and exactly backwards against the camera and the sound.
    return {-std::cos(yaw), 0.0f, std::sin(yaw)};
}

void Player::applyStick(float dx, float dy, float maxRadius) {
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-5f || maxRadius < 1e-5f) { moveInput = vec2(0, 0); return; }

    float mag = len / maxRadius;
    // Dead zone. Without one, a thumb resting on the glass creeps the player
    // forward, which in this game means walking into things in the dark while
    // believing you are standing still.
    if (mag < STICK_DEADZONE) { moveInput = vec2(0, 0); return; }
    // Rescale so the usable range still reaches full speed at the rim.
    mag = clampf((mag - STICK_DEADZONE) / (1.0f - STICK_DEADZONE), 0.0f, 1.0f);

    // Radial, not per-axis. Clamping x and y independently makes the stick a
    // square: pushing diagonally would reach a magnitude of 1.41 and move you
    // faster than pushing straight ahead.
    moveInput = vec2(dx / len * mag, -dy / len * mag);
}

void Player::applyLookDrag(float dx, float dy, float scale) {
    const float base = 0.0038f;
    float s = base * scale * lookSensitivity;
    // Dragging right turns right. Increasing yaw rotates forward toward
    // -right (see hud_math.h), so a rightward drag has to decrease yaw.
    lookInput.x -= dx * s;
    // Screen y grows downward, so an upward drag is a negative dy and must
    // raise the pitch.
    lookInput.y += (invertY ? dy : -dy) * s;
}

vec3 Player::eye() const {
    vec3 e = pos;
    e.y += eyeHeight + std::sin(bobPhase * 2.0f) * 0.035f * bobAmount;
    vec3 r = right();
    float sway = std::sin(bobPhase) * 0.030f * bobAmount;
    e += r * sway;
    return e;
}

vec3 Player::torchDir(float time) const {
    vec3 f = forward();
    // Small, slow hand-drift plus a faster tremor that scales with fear.
    float jx = std::sin(time * 1.7f) * 0.010f + std::sin(time * 11.3f) * 0.004f * (0.3f + fear);
    float jy = std::cos(time * 1.3f) * 0.008f + std::cos(time * 9.7f) * 0.004f * (0.3f + fear);
    vec3 r = right();
    vec3 u = normalize(cross(r, f));
    return normalize(f + r * jx + u * jy);
}

void Player::update(float dt, const World& w, AudioEngine& audio,
                    float creatureDist, bool creatureVisible) {
    bool indoors = w.indoorAt(pos);
    if (!alive) {
        // On death the camera sinks and rolls; the rest of the sim stops.
        eyeHeight = lerpf(eyeHeight, 0.35f, clampf(dt * 2.5f, 0.0f, 1.0f));
        return;
    }

    // --- look ---
    yaw += lookInput.x;
    pitch = clampf(pitch + lookInput.y, -1.45f, 1.45f);
    lookInput = vec2(0, 0);
    while (yaw > PI) yaw -= TAU;
    while (yaw < -PI) yaw += TAU;

    // --- movement ---
    float inLen = length(moveInput);
    if (inLen > 1.0f) moveInput = moveInput * (1.0f / inLen);
    inLen = std::min(inLen, 1.0f);

    bool crouching = crouchHeld;
    bool canSprint = sprintHeld && !crouching && stamina > 0.05f && inLen > 0.5f;

    float targetSpeed = crouching ? 1.35f : (canSprint ? 4.45f : 2.55f);
    float speed = targetSpeed * inLen;
    speedNow = speed;

    if (canSprint) {
        stamina = clampf(stamina - dt * 0.30f, 0.0f, 1.0f);
    } else {
        // Recovery is slow and stalls while you are terrified, so sprinting
        // away from the stalker leaves you unable to do it again immediately.
        float rate = 0.16f * (1.0f - fear * 0.55f);
        stamina = clampf(stamina + dt * rate, 0.0f, 1.0f);
    }

    vec3 f = forward();
    f.y = 0.0f;
    f = normalize(f);
    vec3 r = right();
    vec3 dir = f * moveInput.y + r * moveInput.x;
    float dl = length(dir);
    if (dl > 0.001f) dir = dir / dl;

    // Substep: at sprint speed a dropped frame would otherwise move the player
    // most of a cell in one jump and punch straight through a wall.
    float travel = speed * dt;
    int steps = 1 + (int)(travel / (RADIUS * 0.5f));
    if (steps > 8) steps = 8;
    for (int i = 0; i < steps; i++) {
        vec3 resolved = w.resolveCollision(pos + dir * (travel / steps), RADIUS);
        resolved.y = 0.0f;
        pos = resolved;
    }

    // --- head bob and footsteps ---
    float moving = (speed > 0.15f) ? 1.0f : 0.0f;
    bobAmount = lerpf(bobAmount, moving, clampf(dt * 6.0f, 0.0f, 1.0f));
    float prevBob = bobPhase;
    bobPhase += dt * (speed * 1.55f);
    if (bobPhase > TAU * 64.0f) bobPhase -= TAU * 64.0f;

    if (moving > 0.5f && std::floor(prevBob / PI) != std::floor(bobPhase / PI)) {
        float g = crouching ? 0.20f : (canSprint ? 0.70f : 0.45f);
        audio.post(SND_FOOTSTEP, pos, g, 0.92f + (bobPhase - prevBob) * 0.2f);
        stepCount++;
        // Indoors, some steps land on a board that gives. It is the single
        // most effective way to make the player regret moving quickly, because
        // the noise is theirs and they can hear it carry.
        if (indoors && (stepCount % 3) == 0)
            audio.post(SND_CREAK_FLOOR, pos, crouching ? 0.30f : 0.75f,
                       0.9f + 0.2f * (float)(stepCount % 5) * 0.1f);
    }

    // --- crouch height ---
    float targetEye = crouching ? EYE_CROUCH : EYE_STAND;
    eyeHeight = lerpf(eyeHeight, targetEye, clampf(dt * 9.0f, 0.0f, 1.0f));

    // --- noise the stalker can hear ---
    float baseNoise;
    if (speed < 0.15f)      baseNoise = 0.02f;
    else if (crouching)     baseNoise = 0.09f;
    else if (canSprint)     baseNoise = 0.95f;
    else                    baseNoise = 0.38f;
    noise = lerpf(noise, baseNoise, clampf(dt * 8.0f, 0.0f, 1.0f));

    // --- torch ---
    if (torchOn) {
        battery = clampf(battery - dt * 0.0125f, 0.0f, 1.0f);
        if (battery <= 0.0f) {
            torchOn = false;
            audio.postUI(SND_FLASHLIGHT, 0.7f, 0.8f);
        }
    }
    // A dying battery stutters; a healthy one only shivers.
    float lowBat = 1.0f - smoothstepf(0.0f, 0.28f, battery);
    float base = 0.90f + 0.10f * std::sin(bobPhase * 3.1f);
    float stutter = (lowBat > 0.01f)
        ? (std::sin(bobPhase * 27.0f) * 0.5f + 0.5f) * lowBat : 0.0f;
    torchFlicker = clampf(base - stutter * 0.85f, 0.05f, 1.0f);
    if (!torchOn) torchFlicker = 0.0f;

    // --- fear ---
    // Rises with proximity (whether or not you can see it), spikes on sight,
    // and only bleeds off once it is genuinely far away.
    float proximity = clampf(1.0f - creatureDist / 22.0f, 0.0f, 1.0f);
    float target = proximity * 0.75f;
    if (creatureVisible) target = std::max(target, 0.92f);
    if (!torchOn) target = std::max(target, 0.30f);
    float rate = (target > fear) ? 1.35f : 0.22f;
    fear = clampf(fear + (target - fear) * clampf(dt * rate, 0.0f, 1.0f), 0.0f, 1.0f);

    shake = clampf(shake - dt * 1.4f, 0.0f, 1.0f);
}

} // namespace hm
