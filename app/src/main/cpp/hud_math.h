// hud_math.h - the world-to-HUD transforms, in one place.
//
// The compass and the minimap both need to answer "where is that, relative to
// where I am looking". Getting the handedness of that wrong is invisible in
// code review and immediately obvious to a player, so the maths lives here on
// its own and tests/controls_test.cpp checks it directly.
//
// Conventions used throughout the game:
//   forward(yaw) = ( sin(yaw), 0,  cos(yaw) )
//   right(yaw)   = cross(forward, up) = (-cos(yaw), 0, sin(yaw))
// `right` must match cross(forward, up) because that is the vector the view
// matrix puts along screen-X, and the audio mixer pans against.
#pragma once
#include "hmath.h"

namespace hm {

inline vec3 yawForward(float yaw) {
    return {std::sin(yaw), 0.0f, std::cos(yaw)};
}

inline vec3 yawRight(float yaw) {
    // cross(forward, up) with up = +Y.
    return {-std::cos(yaw), 0.0f, std::sin(yaw)};
}

// Decomposes a world-space offset into the player's frame.
// x = metres to the player's right, y = metres ahead of the player.
inline vec2 worldToLocal(const vec3& offset, float yaw) {
    vec3 f = yawForward(yaw);
    vec3 r = yawRight(yaw);
    return vec2(offset.x * r.x + offset.z * r.z,
                offset.x * f.x + offset.z * f.z);
}

// Minimap position in screen pixels relative to the map centre, for a map
// drawn with the player at the centre facing up. Screen y grows downward, so
// "ahead" has to become a negative y.
inline vec2 minimapOffset(const vec3& offset, float yaw, float pixelsPerMetre) {
    vec2 local = worldToLocal(offset, yaw);
    return vec2(local.x * pixelsPerMetre, -local.y * pixelsPerMetre);
}

// Signed bearing to a target, in radians.
// Positive means the target is to the player's right; zero means dead ahead.
inline float relativeBearing(const vec3& offset, float yaw) {
    vec2 local = worldToLocal(offset, yaw);
    return std::atan2(local.x, local.y);
}

} // namespace hm
