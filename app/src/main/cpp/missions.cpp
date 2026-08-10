#include "missions.h"

namespace hm {

// The chain escalates on two axes: what you are asked to do gets more
// demanding, and how many monsters are awake while you do it goes up.
static const MissionDef kMissions[MISSION_COUNT] = {
    {"CONTACT",        "REACH THE MEETING POINT",        "vo_dispatch_open",  MT_GOTO,     0, false, 0.15f},
    {"SUPPLIES",       "RECOVER 3 SUPPLY CACHES",        "vo_m1_brief",       MT_COLLECT,  3, false, 0.25f},
    {"THE SUBSTATION", "SWITCH ON 2 BREAKERS",           "vo_en_generator",   MT_ACTIVATE, 2, true,  0.35f},
    {"FIRST LIGHT",    "GET BACK TO THE STREET",         "vo_en_power",       MT_GOTO,     0, false, 0.35f},
    {"THE CLINIC",     "FIND THE SURVIVOR INSIDE",       "vo_m2_brief",       MT_RESCUE,   1, true,  0.40f},
    {"WALK THEM HOME", "TAKE THEM TO THE DROP-OFF",      "vo_sv_follow",      MT_RESCUE,   1, false, 0.45f},
    {"SCAVENGE",       "RECOVER 4 SUPPLY CACHES",        "vo_m1_brief",       MT_COLLECT,  4, false, 0.50f},
    {"HOLD POSITION",  "SURVIVE 60 SECONDS",             "vo_warn_close",     MT_SURVIVE, 60, false, 0.60f},
    {"THE TUNNELS",    "CROSS TO THE FAR BLOCK",         "vo_m3_brief",       MT_GOTO,     0, false, 0.60f},
    {"RELAY ONE",      "SWITCH ON 3 RELAYS",             "vo_m4_brief",       MT_ACTIVATE, 3, true,  0.65f},
    {"THE WARD",       "FIND TWO MORE SURVIVORS",        "vo_m2_brief",       MT_RESCUE,   2, true,  0.70f},
    {"BLACKOUT",       "SURVIVE 75 SECONDS IN THE DARK", "vo_warn_dark",      MT_SURVIVE, 75, false, 0.80f},
    {"LAST CACHE",     "RECOVER 5 SUPPLY CACHES",        "vo_hurry",          MT_COLLECT,  5, false, 0.85f},
    {"THE ROOF",       "REACH THE TOWER",                "vo_en_roof",        MT_GOTO,     0, true,  0.90f},
    {"BROADCAST",      "SWITCH ON 4 RELAYS",             "vo_m4_brief",       MT_ACTIVATE, 4, true,  1.00f},
    {"THE HARBOUR",    "GET OUT OF THE DISTRICT",        "vo_m5_brief",       MT_ESCAPE,   0, false, 1.00f},
};

const MissionDef& missionDef(int i) {
    if (i < 0) i = 0;
    if (i >= MISSION_COUNT) i = MISSION_COUNT - 1;
    return kMissions[i];
}

// Picks a spot at least `minDist` from `from`, optionally inside a building.
static bool pickSite(const World& w, const vec3& from, float minDist, float maxDist,
                     bool indoors, Rng& rng, vec3& out) {
    for (int tries = 0; tries < 600; tries++) {
        if (indoors && !w.buildings.empty()) {
            const Building& b = w.buildings[rng.rangei(0, (int)w.buildings.size())];
            if (!b.enterable) continue;
            int x = rng.rangei(b.x0 + 1, b.x1);
            int z = rng.rangei(b.z0 + 1, b.z1);
            if (!w.inBounds(x, z) || !cellIndoor(w.at(x, z))) continue;
            vec3 c = w.cellCenter(x, z);
            float d = length(c - from);
            if (d < minDist || d > maxDist) continue;
            out = c;
            return true;
        }
        int x = rng.rangei(1, w.W - 1);
        int z = rng.rangei(1, w.H - 1);
        if (!w.inBounds(x, z)) continue;
        uint8_t c = w.at(x, z);
        if (c != CELL_ROAD && c != CELL_SIDEWALK && c != CELL_LOT) continue;
        vec3 p = w.cellCenter(x, z);
        float d = length(p - from);
        if (d < minDist || d > maxDist) continue;
        out = p;
        return true;
    }
    // Relax the constraints rather than fail: a mission with no marker is
    // unfinishable, which is worse than a marker that is closer than intended.
    Rng r2(rng.next());
    return w.findOpenNear(from, maxDist, out, r2);
}

void buildMission(MissionState& st, int index, World& w, const vec3& anchor, Rng& rng) {
    st.index = index;
    st.active = true;
    st.complete = false;
    st.collected = 0;
    st.timer = 0.0f;
    st.holdTimer = 0.0f;
    st.objectives.clear();
    st.escortNpc = -1;
    st.escortPickedUp = false;

    const MissionDef& d = missionDef(index);

    // Missions get further away as the chain goes on, so the district opens up
    // gradually instead of dumping the whole map on you at once.
    float span = 30.0f + index * 9.0f;
    float minD = 18.0f + index * 4.0f;
    vec3 site;
    if (!pickSite(w, anchor, minD, minD + span, d.indoors, rng, site)) site = anchor;
    st.marker = site;

    switch (d.type) {
    case MT_GOTO:
    case MT_ESCAPE: {
        Objective o;
        o.pos = site;
        st.objectives.push_back(o);
        break;
    }
    case MT_COLLECT:
    case MT_ACTIVATE: {
        for (int i = 0; i < d.count; i++) {
            vec3 p;
            // Spread them around the marker so the mission is a local sweep,
            // not a single point you stand on.
            if (!pickSite(w, site, 6.0f, 34.0f, d.indoors, rng, p)) p = site;
            Objective o;
            o.pos = p;
            o.pos.y = (d.type == MT_ACTIVATE) ? 1.05f : 0.65f;
            o.bob = rng.range(0.0f, TAU);
            st.objectives.push_back(o);
        }
        st.marker = st.objectives.empty() ? site : st.objectives[0].pos;
        break;
    }
    case MT_RESCUE: {
        for (int i = 0; i < d.count; i++) {
            vec3 p;
            if (!pickSite(w, site, 4.0f, 30.0f, d.indoors, rng, p)) p = site;
            Objective o;
            o.pos = p;
            o.npcId = i;
            st.objectives.push_back(o);
        }
        // The drop-off is always outdoors and well away from the pickup.
        vec3 drop;
        if (!pickSite(w, site, 30.0f, 90.0f, false, rng, drop)) drop = anchor;
        st.dropOff = drop;
        break;
    }
    case MT_SURVIVE: {
        Objective o;
        o.pos = site;
        st.objectives.push_back(o);
        break;
    }
    default:
        break;
    }
}

} // namespace hm
