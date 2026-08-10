// missions.h - the mission chain.
//
// The city is open: you can walk anywhere from the first minute. Progression
// comes from a chain of jobs, each unlocked by the last, with a marker on the
// compass and a radio briefing. Between missions you are free-roaming and the
// monsters are still out there.
#pragma once
#include "hmath.h"
#include "world.h"
#include <string>
#include <vector>

namespace hm {

enum MissionType {
    MT_GOTO = 0,      // reach the marker
    MT_COLLECT,       // gather N items scattered near the marker
    MT_ACTIVATE,      // hold position at N devices to switch them on
    MT_RESCUE,        // find a survivor, then walk them to the drop-off
    MT_SURVIVE,       // stay inside the zone for a set time
    MT_ESCAPE,        // reach the marker with everything hunting you
    MT_TYPE_COUNT
};

struct MissionDef {
    const char* title;
    const char* objective;    // shown on the HUD
    const char* briefVoice;   // asset name of the radio line
    MissionType type;
    int count;                // items / devices / seconds, by type
    bool indoors;             // marker should be placed inside a building
    float monsterPressure;    // 0..1: how many monsters are awake for this job
};

const int MISSION_COUNT = 16;
const MissionDef& missionDef(int i);

struct Objective {
    vec3 pos;
    bool done = false;
    float bob = 0.0f;
    int npcId = -1;      // rescue targets point at an NPC
};

struct MissionState {
    int index = 0;
    bool active = false;
    bool complete = false;
    int collected = 0;
    float timer = 0.0f;
    float holdTimer = 0.0f;
    std::vector<Objective> objectives;
    vec3 marker{0, 0, 0};
    vec3 dropOff{0, 0, 0};
    int escortNpc = -1;
    bool escortPickedUp = false;

    const MissionDef& def() const { return missionDef(index); }
};

// Builds the objectives for a mission. `anchor` biases placement so successive
// missions do not all land in the same corner of the district.
void buildMission(MissionState& st, int index, World& w, const vec3& anchor, Rng& rng);

} // namespace hm
