// platform.h - the small surface where the engine reaches back into the JVM.
//
// Everything here is optional: if the JNI side is unavailable (host builds,
// tests) the calls become no-ops and the game still runs. Nothing in the
// simulation depends on them.
#pragma once

namespace hm {

class Game;

// Must match the constants in Haptics.java.
enum HapticId {
    HAPTIC_LIGHT_POP = 0,
    HAPTIC_FLARE,
    HAPTIC_NEAR_MISS,
    HAPTIC_DEATH,
    HAPTIC_PICKUP
};

// Must match the tiers in DeviceProfile.java.
enum DeviceTier { TIER_LOW = 0, TIER_MID = 1, TIER_HIGH = 2 };

void platformHaptic(HapticId id);
int  platformDeviceTier();
void platformSetGame(Game* g);

} // namespace hm
