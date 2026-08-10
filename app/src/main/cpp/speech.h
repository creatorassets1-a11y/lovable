// speech.h - formant speech synthesis.
//
// This models a vocal tract rather than playing back recordings: a glottal
// source (or noise, for unvoiced sounds) driven through a cascade of five
// resonators whose centre frequencies track phoneme targets. It is a Klatt-
// style synthesiser, which is the honest ceiling for generated speech with no
// recorded audio to draw on.
//
// Every line in the game is then run through a radio channel - band-limited to
// 300-3400Hz, compressed, clipped, with carrier hiss, dropouts and squelch.
// That is a deliberate design choice, not a disguise: a handheld radio is
// exactly the context where a slightly synthetic voice reads as correct.
#pragma once
#include "hmath.h"
#include <vector>
#include <cstdint>

namespace hm {

// ARPABET subset. Enough for natural English without an enormous table.
enum Phoneme {
    PH_SIL = 0,
    // vowels
    PH_IY, PH_IH, PH_EH, PH_AE, PH_AA, PH_AO, PH_UH, PH_UW, PH_AH, PH_ER,
    PH_OW, PH_AY, PH_EY, PH_OY, PH_AW,
    // nasals
    PH_M, PH_N, PH_NG,
    // approximants
    PH_L, PH_R, PH_W, PH_Y,
    // fricatives
    PH_F, PH_V, PH_TH, PH_DH, PH_S, PH_Z, PH_SH, PH_ZH, PH_HH,
    // stops
    PH_P, PH_B, PH_T, PH_D, PH_K, PH_G,
    // affricates
    PH_CH, PH_JH,
    PH_COUNT
};

struct VoiceProfile {
    float pitch = 110.0f;       // base F0 in Hz
    float pitchRange = 0.18f;   // how much the contour moves
    float formantScale = 1.0f;  // <1 longer vocal tract (deeper), >1 shorter
    float breathiness = 0.10f;  // aspiration mixed into the glottal source
    float rate = 1.0f;          // duration multiplier
    float jitter = 0.010f;      // cycle-to-cycle F0 wobble; 0 sounds robotic
    float shimmer = 0.045f;     // cycle-to-cycle amplitude wobble
    float tremor = 0.0f;        // slow wobble - fear, exhaustion
};

// Radio channel parameters. Distance and interference are what sell a voice as
// coming from somewhere else in the world.
struct RadioProfile {
    bool  enabled = true;
    float noise = 0.045f;       // carrier hiss
    float dropout = 0.05f;      // probability of brief signal loss
    float drive = 2.2f;         // soft-clip amount
    float squelch = 1.0f;       // click at open/close
    float lowCut = 300.0f;
    float highCut = 3400.0f;
};

// Parse a space separated phoneme string, e.g. "HH EH L OW . W ER L D".
// '.' inserts a short pause, '|' a longer one, and a trailing digit on a vowel
// marks stress (e.g. "AE1"), which lengthens it and raises the pitch contour.
std::vector<uint8_t> parsePhonemes(const char* s, std::vector<float>* stressOut = nullptr);

// Renders an utterance to mono float samples at `sampleRate`.
std::vector<float> synthesizeUtterance(const char* phonemes,
                                       const VoiceProfile& voice,
                                       const RadioProfile& radio,
                                       int sampleRate,
                                       uint32_t seed = 12345u);

// Non-verbal vocalisations built on the same vocal tract: these are what make
// the human characters sound alive, and the monsters sound like they were once
// something that could speak.
enum VocalType {
    VOC_SCREAM = 0,
    VOC_GASP,
    VOC_PAIN,
    VOC_SOB,
    VOC_LAUGH_BROKEN,
    VOC_MONSTER_ROAR,
    VOC_MONSTER_WAIL,
    VOC_MONSTER_CHITTER,
    VOC_COUNT
};

std::vector<float> synthesizeVocal(VocalType type, const VoiceProfile& voice,
                                   int sampleRate, uint32_t seed);

// Measures how much of a signal's energy sits in the 30-150Hz amplitude
// modulation band - the "roughness" that distinguishes a scream from a shout.
// Returned as a fraction of total modulation energy, 0..1. Used by the tests.
float measureRoughness(const std::vector<float>& pcm, int sampleRate);

} // namespace hm
