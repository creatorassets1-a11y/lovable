#include "story.h"

#include <cstring>
#include <vector>

#include "vo_manifest.h"

namespace hl {
namespace story {

// ================================================================ chapters
//
// The arc is a descent: drier and lighter at the top, flooded and lightless at
// the bottom, with her patience shortening the whole way down.

const Chapter CHAPTERS[CHAPTER_COUNT] = {
    {
        "I", "DESCENT",
        "level/ward.txt", "drone_calm",
        "Find the ward office. Somebody kept records.",
        "Get to the stairwell at the middle of the ward.",
        "READ THE LEDGER", "u_09",
        0.20f, false, false,
        v3(0.045f, 0.047f, 0.060f), v3(0.020f, 0.021f, 0.027f), 0.038f,
        v3(0.95f, 0.42f, 0.18f), 0.9f,
    },
    {
        "II", "THE ROUNDS",
        "level/ward.txt", "drone_dread",
        "She is doing her rounds. Learn the route.",
        "Take the stairwell down.",
        "TOM'S TORCH", "u_03",
        0.45f, false, false,
        v3(0.034f, 0.036f, 0.046f), v3(0.016f, 0.017f, 0.022f), 0.045f,
        v3(0.90f, 0.36f, 0.15f), 0.7f,
    },
    {
        "III", "THE FLOOD",
        "level/flood.txt", "drone_dread",
        "The lower ward is under water. Every step is a shout.",
        "Find the plant room and get through it.",
        "TOM'S BAG", "u_16",
        0.65f, false, true,
        v3(0.028f, 0.032f, 0.044f), v3(0.014f, 0.017f, 0.024f), 0.056f,
        v3(0.50f, 0.62f, 0.85f), 0.55f,
    },
    {
        "IV", "RECORDS",
        "level/records.txt", "drone_chaos",
        "The 1994 file is down here somewhere.",
        "Get out through the west stair.",
        "READ THE FILE", "u_19",
        0.80f, false, false,
        v3(0.024f, 0.024f, 0.030f), v3(0.012f, 0.012f, 0.016f), 0.052f,
        v3(0.85f, 0.30f, 0.12f), 0.5f,
    },
    {
        "V", "THE INCINERATOR",
        "level/records.txt", "drone_chaos",
        "Tom is in the room with the ovens. Get to him.",
        "Take him up through the flue.",
        "TAKE HIS HAND", "u_23",
        1.00f, false, false,
        v3(0.020f, 0.020f, 0.026f), v3(0.010f, 0.010f, 0.014f), 0.060f,
        v3(0.95f, 0.25f, 0.10f), 0.6f,
    },
};

// ==================================================================== banks

namespace {
const char* const SFX_NAMES[] = {
    "step_dry1", "step_dry2", "step_dry3", "step_dry4",
    "step_wet1", "step_wet2", "step_wet3", "step_wet4",
    "drip1", "drip2", "drip3",
    "stinger_a", "stinger_b", "stinger_c",
    "scream_matron", "child_shriek", "gasp", "click", "locker",
    "sub_boom", "riser", "door_creak1", "door_slam", "knock", "scrape",
    "bone", "static", "giggle",
    "drone_calm", "drone_dread", "drone_chaos",
    "water_rise", "light_buzz", "whisper_bed",
    "heart_slow", "heart_fast", "breath_calm", "breath_panic", "ending",
};
}  // namespace
const int SFX_COUNT = (int)(sizeof(SFX_NAMES) / sizeof(SFX_NAMES[0]));

const int VOICE_COUNT = VO_COUNT;

const char* sfx_name(int i) {
    return (i >= 0 && i < SFX_COUNT) ? SFX_NAMES[i] : nullptr;
}

const char* voice_id(int i) {
    return (i >= 0 && i < VO_COUNT) ? VO_LINES[i].id : nullptr;
}

// ================================================================== beats
//
// A beat is a line of dialogue attached to either a moment or a place. Place
// beats are the useful ones: they fire when the player has actually walked into
// the thing being talked about.

struct Beat {
    int chapter;
    float at;              // seconds into the chapter, or -1 for zone-only
    v3 zone;               // world position, or (0,0,0) for time-only
    float radius;
    const char* line;
    const char* objective;
};

const Beat BEATS[] = {
    // --- I: DESCENT
    {1,  2.0f, v3(0, 0, 0), 0, "u_00", nullptr},
    {1,  9.0f, v3(0, 0, 0), 0, "c_00", nullptr},
    {1, 16.0f, v3(0, 0, 0), 0, "u_01", nullptr},
    {1, 34.0f, v3(0, 0, 0), 0, "u_02", nullptr},
    {1, -1.0f, v3(28.0f, 0, 27.0f), 5.0f, "u_04", nullptr},
    {1, -1.0f, v3(44.0f, 0, 27.0f), 6.0f, "u_05", nullptr},
    {1, -1.0f, v3(94.0f, 0, 37.0f), 7.0f, "u_06", "Do not run. She is listening."},
    {1, -1.0f, v3(90.0f, 0, 33.0f), 6.0f, "u_07", nullptr},
    {1, 70.0f, v3(0, 0, 0), 0, "c_01", nullptr},

    // --- II: THE ROUNDS
    {2,  3.0f, v3(0, 0, 0), 0, "u_08", nullptr},
    {2, 20.0f, v3(0, 0, 0), 0, "c_03", nullptr},
    {2, 40.0f, v3(0, 0, 0), 0, "c_04", nullptr},
    {2, -1.0f, v3(24.0f, 0, 19.0f), 5.0f, "u_11", nullptr},
    {2, -1.0f, v3(46.0f, 0, 39.0f), 5.0f, "u_12", nullptr},
    {2, 78.0f, v3(0, 0, 0), 0, "c_05", nullptr},
    {2, -1.0f, v3(70.0f, 0, 27.0f), 6.0f, "u_13", nullptr},

    // --- III: THE FLOOD
    {3,  3.0f, v3(0, 0, 0), 0, "u_14", nullptr},
    {3, 14.0f, v3(0, 0, 0), 0, "u_15", nullptr},
    {3, -1.0f, v3(41.0f, 0, 20.0f), 6.0f, "t_00", nullptr},
    {3, -1.0f, v3(24.0f, 0, 44.0f), 6.0f, "t_01", nullptr},
    {3, 52.0f, v3(0, 0, 0), 0, "c_02", nullptr},
    {3, -1.0f, v3(60.0f, 0, 44.0f), 6.0f, "t_04", nullptr},
    {3, 96.0f, v3(0, 0, 0), 0, "u_17", nullptr},

    // --- IV: RECORDS
    {4,  3.0f, v3(0, 0, 0), 0, "p_00", nullptr},
    {4, 14.0f, v3(0, 0, 0), 0, "p_01", nullptr},
    {4, 22.0f, v3(0, 0, 0), 0, "p_02", nullptr},
    {4, 31.0f, v3(0, 0, 0), 0, "p_03", nullptr},
    {4, 37.0f, v3(0, 0, 0), 0, "p_04", nullptr},
    {4, 43.0f, v3(0, 0, 0), 0, "p_05", nullptr},
    {4, 50.0f, v3(0, 0, 0), 0, "p_06", nullptr},
    {4, 58.0f, v3(0, 0, 0), 0, "u_18", nullptr},
    {4, -1.0f, v3(40.0f, 0, 26.0f), 6.0f, "u_20", nullptr},
    {4, 84.0f, v3(0, 0, 0), 0, "u_21", nullptr},

    // --- V: THE INCINERATOR
    {5,  3.0f, v3(0, 0, 0), 0, "t_05", nullptr},
    {5, -1.0f, v3(40.0f, 0, 52.0f), 8.0f, "t_06", nullptr},
    {5, -1.0f, v3(40.0f, 0, 54.0f), 6.0f, "u_22", nullptr},
    {5, 46.0f, v3(0, 0, 0), 0, "t_09", nullptr},
    {5, -1.0f, v3(10.0f, 0, 44.0f), 6.0f, "u_24", nullptr},
    {5, 76.0f, v3(0, 0, 0), 0, "u_25", nullptr},
    {5, 88.0f, v3(0, 0, 0), 0, "u_26", nullptr},
};
const int BEAT_COUNT = (int)(sizeof(BEATS) / sizeof(BEATS[0]));

namespace {
bool g_fired[BEAT_COUNT] = {};
bool g_note[CHAPTER_COUNT + 1] = {};
}  // namespace

void reset_beats(int chapter) {
    for (int i = 0; i < BEAT_COUNT; i++) {
        if (BEATS[i].chapter == chapter) g_fired[i] = false;
    }
    if (chapter >= 1 && chapter <= CHAPTER_COUNT) g_note[chapter] = false;
}

bool poll_beats(int chapter, float t, v3 pos,
                const char** out_line, const char** out_objective) {
    for (int i = 0; i < BEAT_COUNT; i++) {
        const Beat& b = BEATS[i];
        if (b.chapter != chapter || g_fired[i]) continue;

        bool hit = false;
        if (b.radius > 0) {
            v3 d = pos - b.zone;
            d.y = 0;
            hit = length(d) < b.radius;
        } else if (b.at >= 0) {
            hit = t >= b.at;
        }
        if (!hit) continue;

        g_fired[i] = true;
        *out_line = b.line;
        *out_objective = b.objective;
        return true;
    }
    return false;
}

bool note_taken(int chapter) {
    return chapter >= 1 && chapter <= CHAPTER_COUNT && g_note[chapter];
}

void take_note(int chapter) {
    if (chapter >= 1 && chapter <= CHAPTER_COUNT) g_note[chapter] = true;
}

// ================================================================== lookups

const char* matron_line(bool hunting, unsigned roll) {
    static const char* calm[] = {"m_00", "m_01", "m_02", "m_06", "m_07",
                                 "m_08", "m_11", "m_14"};
    static const char* hunt[] = {"m_03", "m_04", "m_05", "m_09", "m_10", "m_13"};
    if (hunting) return hunt[roll % (sizeof(hunt) / sizeof(hunt[0]))];
    return calm[roll % (sizeof(calm) / sizeof(calm[0]))];
}

namespace {
const VoiceLine* find(const char* id) {
    if (!id) return nullptr;
    for (int i = 0; i < VO_COUNT; i++) {
        if (std::strcmp(VO_LINES[i].id, id) == 0) return &VO_LINES[i];
    }
    return nullptr;
}
}  // namespace

const char* subtitle_for(const char* id) {
    const VoiceLine* v = find(id);
    return v ? v->text : "";
}

const char* speaker_for(const char* id) {
    const VoiceLine* v = find(id);
    if (!v) return "";
    if (std::strcmp(v->who, "ruth") == 0) return "";
    if (std::strcmp(v->who, "control") == 0) return "CONTROL";
    if (std::strcmp(v->who, "matron") == 0) return "THE MATRON";
    if (std::strcmp(v->who, "tom") == 0) return "TOM";
    return "RECORDING";
}

float duration_for(const char* id) {
    const VoiceLine* v = find(id);
    return v ? v->seconds : 2.0f;
}

}  // namespace story
}  // namespace hl
