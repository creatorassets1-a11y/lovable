// Chapter definitions and the beat scheduler.
#pragma once
#include <string>

#include "../core/hmath.h"

namespace hl {
namespace story {

using namespace hm;

struct Chapter {
    const char* title;
    const char* subtitle;
    const char* level;
    const char* bed;

    const char* first_objective;
    const char* second_objective;
    const char* note_label;      // interaction prompt on the document
    const char* note_line;       // voice line played when it is read

    float aggression;            // feeds the Matron's speed and patience
    bool matron_dormant;         // chapter one lets you look around first
    bool flooded;                // water: slower, and much louder

    v3 ambient;
    v3 fog_color;
    float fog_density;
    v3 lamp_color;
    float lamp_intensity;
};

constexpr int CHAPTER_COUNT = 5;
extern const Chapter CHAPTERS[CHAPTER_COUNT];

/** Names of every sfx clip the game loads at boot. */
const char* sfx_name(int i);
extern const int SFX_COUNT;
/** Every voice id, from the generated manifest. */
const char* voice_id(int i);
extern const int VOICE_COUNT;

void reset_beats(int chapter);
/** Fires at most one beat per call. Returns true if something fired. */
bool poll_beats(int chapter, float chapter_time, v3 player_pos,
                const char** out_line, const char** out_objective);

bool note_taken(int chapter);
void take_note(int chapter);

/** A line for the Matron to mutter, chosen by state. */
const char* matron_line(bool hunting, unsigned roll);

const char* subtitle_for(const char* line_id);
const char* speaker_for(const char* line_id);
float duration_for(const char* line_id);

}  // namespace story
}  // namespace hl
