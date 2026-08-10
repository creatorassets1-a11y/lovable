#include "speech.h"
#include <cstring>
#include <cstdio>

namespace hm {

// ---------------------------------------------------------------- phoneme data

enum PhClass { PC_SIL = 0, PC_VOWEL, PC_NASAL, PC_APPROX, PC_FRIC, PC_STOP, PC_AFFR };

struct PhonemeDef {
    const char* name;
    uint8_t cls;
    float f1, f2, f3;        // formant targets at onset
    float e1, e2, e3;        // formant targets at offset (differ for diphthongs)
    float b1, b2, b3;        // bandwidths
    float voiced;            // glottal source amplitude
    float fric;              // frication noise amplitude
    float fricF, fricBW;     // frication band centre and width
    float dur;               // nominal duration, seconds
};

// Formant values are measured male-adult averages (Peterson & Barney and the
// usual Klatt tables). The VoiceProfile scales them for other speakers.
static const PhonemeDef kPh[PH_COUNT] = {
    {"SIL", PC_SIL,   500, 1500, 2500,  500, 1500, 2500,  100, 120, 180, 0.00f, 0.00f,    0,    0, 0.085f},

    {"IY", PC_VOWEL,  270, 2290, 3010,  280, 2250, 2990,   55,  90, 160, 1.00f, 0.00f,    0,    0, 0.115f},
    {"IH", PC_VOWEL,  390, 1990, 2550,  400, 1960, 2540,   60,  95, 165, 1.00f, 0.00f,    0,    0, 0.100f},
    {"EH", PC_VOWEL,  530, 1840, 2480,  540, 1810, 2470,   65, 100, 170, 1.00f, 0.00f,    0,    0, 0.110f},
    {"AE", PC_VOWEL,  660, 1720, 2410,  680, 1690, 2400,   70, 105, 175, 1.00f, 0.00f,    0,    0, 0.130f},
    {"AA", PC_VOWEL,  730, 1090, 2440,  740, 1100, 2430,   75, 100, 175, 1.00f, 0.00f,    0,    0, 0.125f},
    {"AO", PC_VOWEL,  570,  840, 2410,  580,  850, 2400,   70,  95, 175, 1.00f, 0.00f,    0,    0, 0.125f},
    {"UH", PC_VOWEL,  440, 1020, 2240,  450, 1030, 2230,   65,  95, 170, 1.00f, 0.00f,    0,    0, 0.095f},
    {"UW", PC_VOWEL,  300,  870, 2240,  300,  850, 2230,   55,  90, 165, 1.00f, 0.00f,    0,    0, 0.115f},
    {"AH", PC_VOWEL,  640, 1190, 2390,  650, 1200, 2380,   70, 100, 175, 1.00f, 0.00f,    0,    0, 0.090f},
    {"ER", PC_VOWEL,  490, 1350, 1690,  490, 1350, 1690,   65,  95, 130, 1.00f, 0.00f,    0,    0, 0.125f},

    // Diphthongs: onset and offset differ, and the glide between them is what
    // the listener actually identifies.
    {"OW", PC_VOWEL,  490,  910, 2200,  330,  800, 2200,   65,  95, 170, 1.00f, 0.00f,    0,    0, 0.155f},
    {"AY", PC_VOWEL,  730, 1090, 2440,  330, 2100, 2900,   70, 100, 175, 1.00f, 0.00f,    0,    0, 0.175f},
    {"EY", PC_VOWEL,  530, 1840, 2480,  320, 2200, 2950,   65, 100, 170, 1.00f, 0.00f,    0,    0, 0.160f},
    {"OY", PC_VOWEL,  570,  840, 2410,  330, 2100, 2900,   70,  95, 175, 1.00f, 0.00f,    0,    0, 0.185f},
    {"AW", PC_VOWEL,  730, 1090, 2440,  320,  860, 2240,   70, 100, 175, 1.00f, 0.00f,    0,    0, 0.180f},

    // Nasals: low first formant and wide bandwidths approximate the damping of
    // the nasal cavity without a full antiformant branch.
    {"M",  PC_NASAL,  250, 1100, 2200,  250, 1100, 2200,  110, 180, 260, 0.85f, 0.00f,    0,    0, 0.075f},
    {"N",  PC_NASAL,  250, 1700, 2600,  250, 1700, 2600,  110, 180, 260, 0.85f, 0.00f,    0,    0, 0.070f},
    {"NG", PC_NASAL,  250, 2300, 2900,  250, 2300, 2900,  110, 190, 270, 0.85f, 0.00f,    0,    0, 0.080f},

    {"L",  PC_APPROX, 360, 1300, 2700,  380, 1200, 2650,   70, 110, 200, 0.95f, 0.00f,    0,    0, 0.070f},
    {"R",  PC_APPROX, 310, 1060, 1380,  330, 1100, 1450,   70, 100, 140, 0.95f, 0.00f,    0,    0, 0.075f},
    {"W",  PC_APPROX, 290,  610, 2150,  320,  700, 2180,   60,  90, 170, 0.95f, 0.00f,    0,    0, 0.065f},
    {"Y",  PC_APPROX, 260, 2070, 3020,  280, 2000, 2980,   55,  90, 170, 0.95f, 0.00f,    0,    0, 0.060f},

    // Fricatives. Sibilants get a dedicated high band because their energy sits
    // far above the formant range the cascade covers.
    {"F",  PC_FRIC,   400, 1100, 2100,  400, 1100, 2100,   90, 140, 220, 0.00f, 0.32f, 4600, 3200, 0.095f},
    {"V",  PC_FRIC,   400, 1100, 2100,  400, 1100, 2100,   90, 140, 220, 0.45f, 0.18f, 4400, 3000, 0.075f},
    {"TH", PC_FRIC,   400, 1400, 2200,  400, 1400, 2200,   90, 150, 230, 0.00f, 0.26f, 5200, 3400, 0.095f},
    {"DH", PC_FRIC,   400, 1400, 2200,  400, 1400, 2200,   90, 150, 230, 0.45f, 0.14f, 5000, 3200, 0.070f},
    {"S",  PC_FRIC,   320, 1390, 2530,  320, 1390, 2530,   90, 140, 220, 0.00f, 0.62f, 5800, 1900, 0.110f},
    {"Z",  PC_FRIC,   320, 1390, 2530,  320, 1390, 2530,   90, 140, 220, 0.45f, 0.38f, 5600, 1900, 0.085f},
    {"SH", PC_FRIC,   330, 1800, 2600,  330, 1800, 2600,   90, 150, 230, 0.00f, 0.60f, 2900, 1400, 0.115f},
    {"ZH", PC_FRIC,   330, 1800, 2600,  330, 1800, 2600,   90, 150, 230, 0.45f, 0.36f, 2800, 1400, 0.090f},
    {"HH", PC_FRIC,   500, 1500, 2500,  500, 1500, 2500,  120, 180, 260, 0.00f, 0.30f, 1600, 2600, 0.070f},

    // Stops: the definition holds the burst; the closure silence is inserted by
    // the sequencer, because a stop without its silence is unrecognisable.
    {"P",  PC_STOP,   400, 1100, 2200,  400, 1100, 2200,  100, 160, 240, 0.00f, 0.55f, 1100, 1400, 0.080f},
    {"B",  PC_STOP,   350, 1100, 2200,  350, 1100, 2200,  100, 160, 240, 0.40f, 0.30f, 1000, 1200, 0.070f},
    {"T",  PC_STOP,   400, 1700, 2600,  400, 1700, 2600,  100, 160, 240, 0.00f, 0.60f, 4000, 2400, 0.080f},
    {"D",  PC_STOP,   350, 1700, 2600,  350, 1700, 2600,  100, 160, 240, 0.40f, 0.32f, 3600, 2200, 0.070f},
    {"K",  PC_STOP,   400, 1900, 2400,  400, 1900, 2400,  100, 160, 240, 0.00f, 0.58f, 2100, 1800, 0.085f},
    {"G",  PC_STOP,   350, 1900, 2400,  350, 1900, 2400,  100, 160, 240, 0.40f, 0.30f, 1900, 1600, 0.075f},

    {"CH", PC_AFFR,   330, 1800, 2600,  330, 1800, 2600,   90, 150, 230, 0.00f, 0.62f, 2900, 1500, 0.130f},
    {"JH", PC_AFFR,   330, 1800, 2600,  330, 1800, 2600,   90, 150, 230, 0.42f, 0.40f, 2800, 1500, 0.115f},
};

static int phonemeByName(const char* s, int len) {
    for (int i = 0; i < PH_COUNT; i++) {
        if ((int)std::strlen(kPh[i].name) == len && std::strncmp(kPh[i].name, s, len) == 0)
            return i;
    }
    return -1;
}

std::vector<uint8_t> parsePhonemes(const char* s, std::vector<float>* stressOut) {
    std::vector<uint8_t> out;
    if (stressOut) stressOut->clear();
    const char* p = s;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != ' ') p++;
        int len = (int)(p - start);
        if (len <= 0) continue;

        if (len == 1 && start[0] == '.') {
            out.push_back(PH_SIL);
            if (stressOut) stressOut->push_back(0.5f);
            continue;
        }
        if (len == 1 && start[0] == '|') {
            out.push_back(PH_SIL);
            if (stressOut) stressOut->push_back(1.6f);   // longer beat
            continue;
        }

        float stress = 0.0f;
        int nameLen = len;
        if (len > 1 && start[len - 1] >= '0' && start[len - 1] <= '2') {
            stress = (float)(start[len - 1] - '0');
            nameLen = len - 1;
        }
        int id = phonemeByName(start, nameLen);
        if (id < 0) continue;   // unknown token: skip rather than emit noise
        out.push_back((uint8_t)id);
        if (stressOut) stressOut->push_back(stress);
    }
    return out;
}

// ------------------------------------------------------------------- filtering

// Two-pole resonator, the standard Klatt building block.
struct Reson {
    float a = 0, b = 0, c = 0;
    float y1 = 0, y2 = 0;
    void set(float freq, float bw, float fs) {
        if (freq < 20.0f) freq = 20.0f;
        if (freq > fs * 0.48f) freq = fs * 0.48f;
        if (bw < 10.0f) bw = 10.0f;
        c = -std::exp(-2.0f * PI * bw / fs);
        b = 2.0f * std::exp(-PI * bw / fs) * std::cos(2.0f * PI * freq / fs);
        a = 1.0f - b - c;
    }
    float run(float x) {
        float y = a * x + b * y1 + c * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    float run(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
    void lowpass(float f, float q, float fs) {
        float w = TAU * f / fs, cs = std::cos(w), sn = std::sin(w);
        float al = sn / (2.0f * q), a0 = 1.0f + al;
        b0 = (1.0f - cs) * 0.5f / a0;
        b1 = (1.0f - cs) / a0;
        b2 = b0;
        a1 = -2.0f * cs / a0;
        a2 = (1.0f - al) / a0;
    }
    void highpass(float f, float q, float fs) {
        float w = TAU * f / fs, cs = std::cos(w), sn = std::sin(w);
        float al = sn / (2.0f * q), a0 = 1.0f + al;
        b0 = (1.0f + cs) * 0.5f / a0;
        b1 = -(1.0f + cs) / a0;
        b2 = b0;
        a1 = -2.0f * cs / a0;
        a2 = (1.0f - al) / a0;
    }
};

static inline float frand(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return ((int32_t)(s >> 8) * (1.0f / 8388608.0f)) - 1.0f;
}

// ----------------------------------------------------------------- synthesis

struct Frame {
    float f1, f2, f3;
    float b1, b2, b3;
    float voiced, fric;
    float fricF, fricBW;
    float f0Mul;
};

// Builds a per-sample-block parameter track from the phoneme sequence, with
// coarticulation: formants glide between neighbouring targets instead of
// jumping, which is most of what makes synthetic speech intelligible.
static std::vector<Frame> buildFrames(const std::vector<uint8_t>& phs,
                                      const std::vector<float>& stress,
                                      const VoiceProfile& voice,
                                      int sampleRate, int frameSamples,
                                      std::vector<float>& f0Track) {
    struct Seg { int ph; float dur; float stress; bool closure; };
    std::vector<Seg> segs;

    for (size_t i = 0; i < phs.size(); i++) {
        const PhonemeDef& d = kPh[phs[i]];
        float st = (i < stress.size()) ? stress[i] : 0.0f;
        // Stops and affricates need a silent closure before the burst.
        if (d.cls == PC_STOP || d.cls == PC_AFFR) {
            segs.push_back({PH_SIL, (d.voiced > 0.1f ? 0.045f : 0.055f) * voice.rate, 0.0f, true});
        }
        float dur = d.dur;
        if (phs[i] == PH_SIL) dur *= (st > 0.0f ? st : 1.0f);
        else if (st >= 1.0f) dur *= 1.28f;      // stressed syllables are longer
        else if (d.cls == PC_VOWEL) dur *= 0.92f;
        segs.push_back({phs[i], dur * voice.rate, st, false});
    }
    if (segs.empty()) return {};

    float total = 0.0f;
    for (const Seg& s : segs) total += s.dur;
    int totalFrames = std::max(1, (int)(total * sampleRate / frameSamples));

    std::vector<Frame> frames(totalFrames);
    f0Track.assign(totalFrames, voice.pitch);

    float fscale = voice.formantScale;
    int fi = 0;
    for (size_t si = 0; si < segs.size(); si++) {
        const Seg& seg = segs[si];
        const PhonemeDef& d = kPh[seg.ph];
        int n = std::max(1, (int)(seg.dur * sampleRate / frameSamples));
        for (int k = 0; k < n && fi < totalFrames; k++, fi++) {
            float t = (n > 1) ? (float)k / (n - 1) : 0.0f;
            Frame& f = frames[fi];
            f.f1 = lerpf(d.f1, d.e1, t) * fscale;
            f.f2 = lerpf(d.f2, d.e2, t) * fscale;
            f.f3 = lerpf(d.f3, d.e3, t) * fscale;
            f.b1 = d.b1; f.b2 = d.b2; f.b3 = d.b3;
            f.voiced = seg.closure ? (d.voiced > 0.0f ? 0.0f : 0.0f) : d.voiced;
            f.fric = seg.closure ? 0.0f : d.fric;
            f.fricF = d.fricF * fscale;
            f.fricBW = d.fricBW;

            // Stop bursts are an impulse of frication at release, not a plateau.
            if (!seg.closure && (d.cls == PC_STOP || d.cls == PC_AFFR)) {
                float burst = std::exp(-t * (d.cls == PC_STOP ? 16.0f : 5.0f));
                f.fric = d.fric * burst;
                if (d.cls == PC_STOP) f.voiced = d.voiced * (t > 0.35f ? 1.0f : 0.15f);
            }

            // Pitch contour: a gentle declination across the utterance with a
            // lift on stressed syllables. Flat F0 is the single biggest reason
            // synthetic speech sounds dead.
            float progress = (float)fi / std::max(1, totalFrames - 1);
            float decl = 1.0f - 0.18f * progress;
            float accent = (seg.stress >= 1.0f) ? (1.0f + voice.pitchRange * 0.65f
                             * std::sin(t * PI)) : 1.0f;
            f0Track[fi] = voice.pitch * decl * accent;
        }
    }
    for (; fi < totalFrames; fi++) {
        frames[fi] = frames[fi > 0 ? fi - 1 : 0];
        f0Track[fi] = f0Track[fi > 0 ? fi - 1 : 0];
    }

    // Smooth the formant tracks. Real articulators have mass; instantaneous
    // formant jumps sound like a synthesiser changing patch.
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 1; i < totalFrames; i++) {
            frames[i].f1 = lerpf(frames[i - 1].f1, frames[i].f1, 0.45f);
            frames[i].f2 = lerpf(frames[i - 1].f2, frames[i].f2, 0.42f);
            frames[i].f3 = lerpf(frames[i - 1].f3, frames[i].f3, 0.40f);
            frames[i].voiced = lerpf(frames[i - 1].voiced, frames[i].voiced, 0.55f);
        }
        for (int i = totalFrames - 2; i >= 0; i--) {
            frames[i].f1 = lerpf(frames[i + 1].f1, frames[i].f1, 0.75f);
            frames[i].f2 = lerpf(frames[i + 1].f2, frames[i].f2, 0.72f);
            frames[i].f3 = lerpf(frames[i + 1].f3, frames[i].f3, 0.72f);
        }
    }
    return frames;
}

// Rosenberg glottal pulse: the asymmetric open/close shape of real vocal folds.
// A plain impulse train gives a buzzy, obviously-artificial timbre.
static inline float glottalPulse(float phase) {
    const float T1 = 0.42f, T2 = 0.20f;
    if (phase < T1) {
        float x = phase / T1;
        return 0.5f * (1.0f - std::cos(PI * x));
    } else if (phase < T1 + T2) {
        float x = (phase - T1) / T2;
        return std::cos(0.5f * PI * x);
    }
    return 0.0f;
}

static void applyRadio(std::vector<float>& sig, const RadioProfile& r,
                       int sampleRate, uint32_t& rng) {
    if (!r.enabled) return;

    Biquad hp, lp;
    hp.highpass(r.lowCut, 0.72f, (float)sampleRate);
    lp.lowpass(r.highCut, 0.72f, (float)sampleRate);

    // Dropouts are a slow random gate, not per-sample noise, so they sound like
    // a signal being lost rather than like distortion.
    float gate = 1.0f, gateTarget = 1.0f;
    int gateTimer = 0;

    for (size_t i = 0; i < sig.size(); i++) {
        float x = sig[i];
        x = hp.run(x);
        x = lp.run(x);

        // Radio compression: quiet parts pushed up, peaks clipped.
        x = std::tanh(x * r.drive) / std::tanh(r.drive);

        if (gateTimer-- <= 0) {
            gateTimer = (int)(sampleRate * 0.02f);
            float roll = (frand(rng) + 1.0f) * 0.5f;
            gateTarget = (roll < r.dropout) ? (frand(rng) * 0.15f + 0.15f) : 1.0f;
        }
        gate = lerpf(gate, gateTarget, 0.06f);
        x *= gate;

        x += frand(rng) * r.noise * (0.5f + 0.5f * gate);
        sig[i] = x;
    }

    // Squelch: the click and hiss burst as the channel opens and closes. This
    // is the detail that makes people believe it is a radio.
    if (r.squelch > 0.0f) {
        int n = std::min((int)sig.size(), (int)(sampleRate * 0.05f));
        for (int i = 0; i < n; i++) {
            float env = std::exp(-i / (sampleRate * 0.012f));
            sig[i] += frand(rng) * 0.30f * env * r.squelch;
        }
        int tail = (int)(sampleRate * 0.07f);
        int start = std::max(0, (int)sig.size() - tail);
        for (int i = start; i < (int)sig.size(); i++) {
            float env = 1.0f - (float)(i - start) / std::max(1, tail);
            sig[i] += frand(rng) * 0.22f * env * env * r.squelch;
        }
    }
}

std::vector<float> synthesizeUtterance(const char* phonemes,
                                       const VoiceProfile& voice,
                                       const RadioProfile& radio,
                                       int sampleRate,
                                       uint32_t seed) {
    std::vector<float> stress;
    std::vector<uint8_t> phs = parsePhonemes(phonemes, &stress);
    if (phs.empty()) return {};

    const int frameSamples = 64;   // ~1.5ms at 44.1k: fine enough for bursts
    std::vector<float> f0Track;
    std::vector<Frame> frames = buildFrames(phs, stress, voice, sampleRate,
                                            frameSamples, f0Track);
    if (frames.empty()) return {};

    uint32_t rng = seed ? seed : 1u;
    std::vector<float> out;
    out.reserve(frames.size() * frameSamples + sampleRate / 4);

    Reson r1, r2, r3, r4, r5, rFric;
    float fs = (float)sampleRate;
    // F4 and F5 are fixed: they carry voice quality rather than phoneme
    // identity, so they do not need to track the targets.
    r4.set(3400.0f * voice.formantScale, 220.0f, fs);
    r5.set(4500.0f * voice.formantScale, 260.0f, fs);

    float phase = 0.0f;
    float periodJitter = 1.0f;
    float ampShimmer = 1.0f;
    float tremorPhase = 0.0f;
    // Radiation from the lips is a first-order differentiator (roughly
    // +6dB/octave). The glottal source is a flow waveform, so without this the
    // output is dominated by the fundamental and the formants are buried -
    // every vowel measures the same and none of them are identifiable.
    float radiationPrev = 0.0f;

    for (size_t fi = 0; fi < frames.size(); fi++) {
        const Frame& f = frames[fi];
        r1.set(f.f1, f.b1, fs);
        r2.set(f.f2, f.b2, fs);
        r3.set(f.f3, f.b3, fs);
        if (f.fric > 0.0001f && f.fricF > 0.0f) rFric.set(f.fricF, f.fricBW, fs);

        for (int k = 0; k < frameSamples; k++) {
            float f0 = f0Track[fi];
            tremorPhase += 5.5f / fs;
            f0 *= 1.0f + voice.tremor * std::sin(tremorPhase * TAU);
            f0 *= periodJitter;

            phase += f0 / fs;
            if (phase >= 1.0f) {
                phase -= 1.0f;
                // New glottal cycle: re-roll jitter and shimmer. Perfectly
                // periodic phonation is the classic robot giveaway.
                periodJitter = 1.0f + frand(rng) * voice.jitter;
                ampShimmer = 1.0f + frand(rng) * voice.shimmer;
            }

            float glottal = glottalPulse(phase) * ampShimmer;
            float aspiration = frand(rng) * voice.breathiness;
            float src = (glottal + aspiration) * f.voiced;

            // Cascade: each resonator feeds the next, which is what produces
            // the correct relative formant amplitudes.
            float v = r1.run(src);
            v = r2.run(v);
            v = r3.run(v);
            v = r4.run(v);
            v = r5.run(v);

            float fricSig = 0.0f;
            if (f.fric > 0.0001f) fricSig = rFric.run(frand(rng)) * f.fric * 2.4f;

            float mixed = v * 2.2f + fricSig;
            float radiated = mixed - radiationPrev;
            radiationPrev = mixed;
            out.push_back(radiated);
        }
    }

    // Normalise before the channel so the radio compressor behaves predictably.
    float peak = 1e-6f;
    for (float s : out) peak = std::max(peak, std::fabs(s));
    float g = 0.85f / peak;
    for (float& s : out) s *= g;

    applyRadio(out, radio, sampleRate, rng);

    peak = 1e-6f;
    for (float s : out) peak = std::max(peak, std::fabs(s));
    g = 0.92f / peak;
    for (float& s : out) s *= g;
    return out;
}

// ------------------------------------------------------------- vocalisations

std::vector<float> synthesizeVocal(VocalType type, const VoiceProfile& voice,
                                   int sampleRate, uint32_t seed) {
    uint32_t rng = seed ? seed : 7u;
    float fs = (float)sampleRate;

    struct Spec { float dur; float f0a, f0b; float f1, f2, f3; float noise; float rough; };
    static const Spec kSpec[VOC_COUNT] = {
        // dur   f0 start/end     F1    F2    F3    noise  rough
        {1.90f, 1.85f, 1.35f,  850, 1500, 2600, 0.30f, 0.55f},  // SCREAM
        {0.65f, 1.10f, 0.85f,  600, 1300, 2400, 0.75f, 0.10f},  // GASP
        {1.10f, 1.30f, 0.80f,  620, 1150, 2350, 0.25f, 0.35f},  // PAIN
        {1.70f, 0.95f, 0.70f,  480, 1100, 2300, 0.35f, 0.25f},  // SOB
        {2.10f, 1.05f, 0.75f,  620, 1250, 2400, 0.30f, 0.45f},  // LAUGH_BROKEN
        {2.30f, 0.42f, 0.30f,  320,  780, 1900, 0.45f, 0.85f},  // MONSTER_ROAR
        {2.90f, 0.70f, 1.25f,  520, 1450, 2700, 0.35f, 0.70f},  // MONSTER_WAIL
        {1.40f, 1.60f, 1.50f,  700, 2100, 3200, 0.55f, 0.90f},  // MONSTER_CHITTER
    };
    const Spec& sp = kSpec[type];

    int n = (int)(sp.dur * fs);
    std::vector<float> out(n, 0.0f);

    Reson r1, r2, r3;
    r1.set(sp.f1 * voice.formantScale, 90.0f, fs);
    r2.set(sp.f2 * voice.formantScale, 130.0f, fs);
    r3.set(sp.f3 * voice.formantScale, 200.0f, fs);

    float phase = 0.0f, jitter = 1.0f, shim = 1.0f;
    float sobPhase = 0.0f;
    float radiationPrev = 0.0f;

    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        float env;
        switch (type) {
        case VOC_SCREAM:
            env = smoothstepf(0.0f, 0.05f, t) * (1.0f - smoothstepf(0.55f, 1.0f, t));
            break;
        case VOC_GASP:
            env = std::sin(t * PI);
            env *= env;
            break;
        case VOC_SOB:
        case VOC_LAUGH_BROKEN: {
            // Broken into syllabic pulses rather than one continuous tone.
            sobPhase += (type == VOC_SOB ? 2.6f : 5.4f) / fs;
            float pulse = 0.5f + 0.5f * std::sin(sobPhase * TAU);
            env = pulse * pulse * (1.0f - smoothstepf(0.6f, 1.0f, t));
            break;
        }
        case VOC_MONSTER_CHITTER: {
            sobPhase += 14.0f / fs;
            float pulse = 0.5f + 0.5f * std::sin(sobPhase * TAU);
            env = pulse * pulse * pulse * (1.0f - smoothstepf(0.7f, 1.0f, t));
            break;
        }
        default:
            env = smoothstepf(0.0f, 0.08f, t) * (1.0f - smoothstepf(0.6f, 1.0f, t));
            break;
        }

        float f0 = voice.pitch * lerpf(sp.f0a, sp.f0b, t);
        f0 *= jitter;
        phase += f0 / fs;
        if (phase >= 1.0f) {
            phase -= 1.0f;
            // Roughness is jitter turned up until the folds stop being periodic
            // - it is what separates a shout from a scream, and a scream from
            // something that does not have human vocal folds at all.
            jitter = 1.0f + frand(rng) * (voice.jitter + sp.rough * 0.30f);
            shim = 1.0f + frand(rng) * (voice.shimmer + sp.rough * 0.45f);
        }

        float glottal = glottalPulse(phase) * shim;
        float src = glottal + frand(rng) * sp.noise;
        if (sp.rough > 0.5f) src = std::tanh(src * (1.0f + sp.rough * 3.0f));

        float v = r1.run(src * env);
        v = r2.run(v);
        v = r3.run(v);
        float mixed = v * 2.6f;
        out[i] = mixed - radiationPrev;   // lip radiation, as above
        radiationPrev = mixed;
    }

    float peak = 1e-6f;
    for (float s : out) peak = std::max(peak, std::fabs(s));
    float g = 0.92f / peak;
    for (float& s : out) s *= g;
    return out;
}

} // namespace hm
