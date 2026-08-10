#include "audio.h"
#include <cstring>

// HOLLOW_NO_OPENSL builds the synthesis engine without the Android backend, so
// the DSP can be compiled and exercised on a host machine by the tests.
#if !defined(HOLLOW_NO_OPENSL)
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <android/log.h>
#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, "HollowAudio", __VA_ARGS__)
#else
#include <cstdio>
#define ALOG(...) do { } while (0)
#endif

namespace hm {

static const float kInvSR = 1.0f / (float)AudioEngine::SAMPLE_RATE;

// Duration of each one-shot, in seconds. Voices free themselves when t > dur.
static const float kDur[SND_COUNT] = {
    0.26f,  // FOOTSTEP
    0.40f,  // FOOTSTEP_WET
    0.55f,  // CREATURE_STEP
    2.00f,  // STINGER
    1.70f,  // SCREECH
    1.50f,  // GROWL
    0.90f,  // CHITTER
    2.40f,  // WATCHER_CALL
    0.80f,  // PICKUP
    1.40f,  // DOOR
    0.10f,  // FLASHLIGHT
    0.60f,  // DRIP
    1.60f,  // METAL
    1.60f,  // WHISPER
    0.40f,  // LIGHT_POP
    2.60f,  // EXIT_OPEN
    3.00f,  // DEATH
    0.36f,  // HEARTBEAT_THUMP
    1.00f,  // BREATH
    1.20f,  // FLARE_FIRE
    0.35f,  // RADIO_BEEP
    1.00f, 1.00f, 1.00f, 1.00f,   // NPC_*: always resolved to baked samples
};

// How much of each sound feeds the reverb. Dry impacts stay close; screams and
// metal ring down the whole street.
static const float kSend[SND_COUNT] = {
    0.18f, 0.24f, 0.30f, 0.75f, 0.70f, 0.40f, 0.45f, 0.80f,
    0.45f, 0.35f, 0.05f, 0.65f, 0.72f, 0.55f, 0.40f, 0.60f,
    0.65f, 0.05f, 0.15f, 0.50f, 0.10f,
    0.40f, 0.40f, 0.40f, 0.40f,
};

// Ids that are played from baked audio rather than synthesised. Resolved once
// at start-up; post() picks a take so a line never repeats back to back.
struct IdSampleSet { const char* base; int takes; };
static const IdSampleSet kIdSamples[SND_COUNT] = {
    {nullptr,0},{nullptr,0},{nullptr,0},{nullptr,0},{nullptr,0},{nullptr,0},
    {"vx_chitter",3},          // CHITTER
    {"vx_wail",3},             // WATCHER_CALL
    {nullptr,0},{nullptr,0},{nullptr,0},{nullptr,0},{nullptr,0},{nullptr,0},
    {nullptr,0},{nullptr,0},{nullptr,0},{nullptr,0},{nullptr,0},{nullptr,0},
    {nullptr,0},
    {"vo_sv_help",1},          // NPC_HELP
    {"vx_sob",3},              // NPC_SOB
    {"vo_sv_run",1},           // NPC_RUN
    {"vo_sv_follow",1},        // NPC_FOLLOW
};

// ------------------------------------------------------------- event plumbing

void AudioEngine::setPack(const AssetPack* pack) { mPack = pack; }

void AudioEngine::buildSampleTable() {
    mSamples.clear();
    if (!mPack || !mPack->valid()) return;
    for (int i = 0; i < mPack->count(); i++) {
        const PakEntry* e = mPack->entryAt(i);
        if (!e) continue;
        if (e->type != PAK_AUDIO_MONO && e->type != PAK_AUDIO_STEREO) continue;
        Sample s;
        s.pcm = reinterpret_cast<const int16_t*>(mPack->dataOf(*e));
        s.rate = e->w;
        s.frames = e->h;
        s.stereo = (e->type == PAK_AUDIO_STEREO);
        std::memcpy(s.name, e->name, PAK_NAME_LEN);
        mSamples.push_back(s);
    }
    ALOG("audio: %d baked samples", (int)mSamples.size());
}

int AudioEngine::sampleIndex(const char* name) const {
    for (size_t i = 0; i < mSamples.size(); i++)
        if (std::strncmp(mSamples[i].name, name, PAK_NAME_LEN) == 0) return (int)i;
    return -1;
}

int AudioEngine::sampleIndexRandom(const char* base, int takes, uint32_t seed) const {
    if (takes <= 1) return sampleIndex(base);
    char buf[PAK_NAME_LEN];
    std::snprintf(buf, sizeof(buf), "%s_%d", base, (int)(seed % (uint32_t)takes));
    int idx = sampleIndex(buf);
    if (idx >= 0) return idx;
    return sampleIndex(base);
}

void AudioEngine::pushEvent(const SoundEvent& e) {
    uint32_t w = mWrite.load(std::memory_order_relaxed);
    uint32_t r = mRead.load(std::memory_order_acquire);
    if (w - r >= RING_SIZE) return;   // ring full: drop, never block the game
    mRing[w % RING_SIZE] = e;
    mWrite.store(w + 1, std::memory_order_release);
}

void AudioEngine::post(SoundId id, const vec3& pos, float gain, float pitch) {
    SoundEvent e{};
    e.id = id;
    e.sample = -1;
    // Some ids are baked rather than synthesised. Resolving here keeps the
    // string lookup on the game thread, where it is allowed to be slow.
    if (id >= 0 && id < SND_COUNT && kIdSamples[id].base) {
        static uint32_t rot = 0;
        e.sample = sampleIndexRandom(kIdSamples[id].base, kIdSamples[id].takes, rot++);
    }
    e.x = pos.x; e.y = pos.y; e.z = pos.z;
    e.gain = gain; e.pitch = pitch;
    e.flags = (pos.x > 1e8f) ? 1 : 0;
    pushEvent(e);
}

void AudioEngine::postUI(SoundId id, float gain, float pitch) {
    post(id, vec3(1e9f, 1e9f, 1e9f), gain, pitch);
}

void AudioEngine::postSample(int sampleIdx, const vec3& pos, float gain, float pitch) {
    if (sampleIdx < 0 || sampleIdx >= (int)mSamples.size()) return;
    SoundEvent e{};
    e.id = -1;
    e.sample = sampleIdx;
    e.x = pos.x; e.y = pos.y; e.z = pos.z;
    e.gain = gain; e.pitch = pitch;
    e.flags = 0;
    pushEvent(e);
}

void AudioEngine::postVoice(int sampleIdx, float gain) {
    if (sampleIdx < 0 || sampleIdx >= (int)mSamples.size()) return;
    SoundEvent e{};
    e.id = -1;
    e.sample = sampleIdx;
    e.x = e.y = e.z = 0.0f;
    e.gain = gain;
    e.pitch = 1.0f;
    e.flags = 1 | 2;    // non-positional dialogue
    pushEvent(e);
}

void AudioEngine::setListener(const vec3& pos, const vec3& forward) {
    mLx.store(pos.x); mLy.store(pos.y); mLz.store(pos.z);
    vec3 f = normalize(forward);
    mFx.store(f.x); mFy.store(f.y); mFz.store(f.z);
}

void AudioEngine::triggerVoice(const SoundEvent& e) {
    // Steal the oldest voice if all are busy - dropping a scream is worse than
    // cutting a footstep short.
    int slot = -1;
    float oldest = -1.0f;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!mVoices[i].active) { slot = i; break; }
        if (mVoices[i].t > oldest) { oldest = mVoices[i].t; slot = i; }
    }
    if (slot < 0) return;
    Voice& v = mVoices[slot];
    v.active = true;
    v.id = e.id;
    v.sample = e.sample;
    v.t = 0.0f;
    v.cursor = 0.0;
    if (e.sample >= 0 && e.sample < (int)mSamples.size()) {
        const Sample& sm = mSamples[e.sample];
        v.dur = sm.frames / (float)sm.rate / std::max(0.25f, e.pitch) + 0.02f;
    } else {
        v.dur = kDur[e.id >= 0 ? e.id : 0] / std::max(0.25f, e.pitch);
    }
    v.gain = e.gain;
    v.pitch = e.pitch;
    v.pos = vec3(e.x, e.y, e.z);
    v.dialogue = (e.flags & 2) != 0;
    v.positional = (e.flags & 1) == 0;
    v.ph1 = v.ph2 = v.ph3 = 0.0f;
    v.lp1 = v.lp2 = v.bp1 = v.bp2 = 0.0f;
    v.rng = (uint32_t)(0x9E3779B9u + slot * 2654435761u + (uint32_t)(e.gain * 100003.0f));
    if (!v.rng) v.rng = 1u;
}

// ------------------------------------------------------------------ synthesis

float AudioEngine::renderVoice(Voice& v, float dt, float& sendOut) {
    float t = v.t;
    float p = v.dur > 0.0f ? clampf(t / v.dur, 0.0f, 1.0f) : 1.0f;
    float s = 0.0f;
    float n = noise(v.rng);

    switch (v.id) {
    case SND_FOOTSTEP:
    case SND_FOOTSTEP_WET: {
        // Body: a short thump. Detail: grit filtered into a narrow band.
        float env = std::exp(-t * (v.id == SND_FOOTSTEP ? 26.0f : 14.0f));
        float thumpF = 78.0f * v.pitch * (1.0f - 0.45f * p);
        v.ph1 += thumpF * dt;
        float thump = std::sin(v.ph1 * TAU) * std::exp(-t * 34.0f);
        // 2-pole bandpass on noise
        float cut = (v.id == SND_FOOTSTEP ? 0.28f : 0.16f) * (1.0f - 0.5f * p);
        v.lp1 += (n - v.lp1) * cut;
        v.lp2 += (v.lp1 - v.lp2) * cut;
        float grit = (v.lp1 - v.lp2) * 3.0f;
        s = thump * 0.85f + grit * env * 0.55f;
        break;
    }
    case SND_CREATURE_STEP: {
        // Much lower and slower than the player's step, with a dragged scrape.
        float env = std::exp(-t * 7.0f);
        float f = 44.0f * v.pitch * (1.0f - 0.5f * p);
        v.ph1 += f * dt;
        float body = std::sin(v.ph1 * TAU);
        body = std::tanh(body * 2.2f);
        v.lp1 += (n - v.lp1) * 0.05f;
        float scrape = v.lp1 * 2.5f * std::exp(-t * 3.5f) * smoothstepf(0.0f, 0.15f, p);
        s = body * env * 1.1f + scrape * 0.5f;
        break;
    }
    case SND_STINGER: {
        // Detuned tritone cluster + a noise rise that stops dead. The silence
        // after the cut is the part that actually lands.
        float env = (p < 0.02f) ? p / 0.02f : std::exp(-(t - 0.02f) * 2.6f);
        float base = 138.0f * v.pitch;
        v.ph1 += base * dt;
        v.ph2 += base * 1.4142f * dt;          // tritone
        v.ph3 += base * 2.0f * 1.007f * dt;    // beating octave
        float cluster = std::sin(v.ph1 * TAU) + std::sin(v.ph2 * TAU) * 0.8f
                      + std::sin(v.ph3 * TAU) * 0.5f;
        float riseCut = clampf(0.02f + p * 0.55f, 0.0f, 0.9f);
        v.lp1 += (n - v.lp1) * riseCut;
        float rise = v.lp1 * (p < 0.45f ? p * 2.2f : 0.0f);
        s = std::tanh(cluster * 0.55f) * env * 0.7f + rise * 0.4f;
        break;
    }
    case SND_SCREECH: {
        // FM with a high modulation index, swept upward, then hard-clipped.
        float env = smoothstepf(0.0f, 0.06f, p) * std::exp(-t * 1.9f);
        float carrier = 520.0f * v.pitch * (1.0f + p * 1.6f);
        float modf = carrier * 0.37f;
        v.ph2 += modf * dt;
        float mod = std::sin(v.ph2 * TAU) * (7.0f - p * 3.0f);
        v.ph1 += carrier * dt;
        float fm = std::sin((v.ph1 + mod * 0.15f) * TAU);
        v.lp1 += (n - v.lp1) * 0.55f;
        s = std::tanh((fm * 1.7f + v.lp1 * 0.7f)) * env * 0.75f;
        break;
    }
    case SND_GROWL: {
        // Sub-bass saw, amplitude-modulated so it sounds like it has lungs.
        float env = smoothstepf(0.0f, 0.12f, p) * (1.0f - smoothstepf(0.7f, 1.0f, p));
        float f = 62.0f * v.pitch;
        v.ph1 += f * dt;
        if (v.ph1 > 1.0f) v.ph1 -= 1.0f;
        float saw = v.ph1 * 2.0f - 1.0f;
        v.ph2 += 17.0f * dt;
        float am = 0.62f + 0.38f * std::sin(v.ph2 * TAU);
        v.lp1 += (saw * am - v.lp1) * 0.09f;
        v.lp2 += (v.lp1 - v.lp2) * 0.09f;
        v.lp1 += (n * 0.25f - v.lp1) * 0.02f;
        s = v.lp2 * env * 1.6f;
        break;
    }
    case SND_FLARE_FIRE: {
        // Sharp crack, then the hiss of the flare burning.
        float crack = n * std::exp(-t * 40.0f) * 1.4f;
        v.lp1 += (n - v.lp1) * 0.30f;
        v.lp2 += (v.lp1 - v.lp2) * 0.30f;
        float hiss = (v.lp1 - v.lp2) * 3.0f * std::exp(-t * 1.6f);
        v.ph1 += 140.0f * dt;
        float whoosh = std::sin(v.ph1 * TAU) * std::exp(-t * 6.0f) * 0.4f;
        s = std::tanh(crack + whoosh) * 0.7f + hiss * 0.5f;
        break;
    }
    case SND_RADIO_BEEP: {
        // Two-tone squelch, the sound of a channel opening.
        float env = (p < 0.5f) ? 1.0f : 0.0f;
        v.ph1 += (p < 0.25f ? 880.0f : 1180.0f) * dt;
        s = std::sin(v.ph1 * TAU) * env * 0.28f * std::exp(-t * 2.0f);
        s += n * 0.05f * env;
        break;
    }
    case SND_PICKUP: {
        // A minor second, not a major third: reads as "wrong", not "reward".
        float env = std::exp(-t * 4.5f);
        v.ph1 += 880.0f * v.pitch * dt;
        v.ph2 += 932.0f * v.pitch * dt;
        s = (std::sin(v.ph1 * TAU) * 0.6f + std::sin(v.ph2 * TAU) * 0.35f) * env * 0.5f;
        break;
    }
    case SND_DOOR: {
        // Stick-slip creak: noise through a bandpass whose centre wanders.
        float env = smoothstepf(0.0f, 0.05f, p) * (1.0f - smoothstepf(0.75f, 1.0f, p));
        v.ph2 += 6.5f * dt;
        float wob = 0.05f + 0.035f * std::sin(v.ph2 * TAU) + 0.02f * p;
        v.lp1 += (n - v.lp1) * wob;
        v.lp2 += (v.lp1 - v.lp2) * wob;
        float creak = (v.lp1 - v.lp2) * 7.0f;
        float clunk = (p > 0.82f) ? std::sin((t - v.dur * 0.82f) * 90.0f * TAU)
                                     * std::exp(-(t - v.dur * 0.82f) * 22.0f) : 0.0f;
        s = creak * env * 0.5f + clunk * 0.6f;
        break;
    }
    case SND_FLASHLIGHT: {
        float env = std::exp(-t * 90.0f);
        v.ph1 += 2400.0f * dt;
        s = (n * 0.6f + std::sin(v.ph1 * TAU) * 0.4f) * env * 0.5f;
        break;
    }
    case SND_DRIP: {
        // Downward chirp - the classic water-in-a-dark-room cue.
        float env = std::exp(-t * 11.0f);
        float f = 1500.0f * v.pitch * std::exp(-t * 9.0f) + 180.0f;
        v.ph1 += f * dt;
        s = std::sin(v.ph1 * TAU) * env * 0.42f;
        break;
    }
    case SND_METAL: {
        // Three inharmonic modes = struck metal rather than a tuned bell.
        float env = std::exp(-t * 2.4f);
        v.ph1 += 410.0f * v.pitch * dt;
        v.ph2 += 410.0f * 2.76f * v.pitch * dt;
        v.ph3 += 410.0f * 5.40f * v.pitch * dt;
        s = (std::sin(v.ph1 * TAU) * 0.5f
           + std::sin(v.ph2 * TAU) * 0.32f * std::exp(-t * 3.6f)
           + std::sin(v.ph3 * TAU) * 0.18f * std::exp(-t * 5.5f)) * env * 0.5f;
        break;
    }
    case SND_WHISPER: {
        // Noise through a sliding formant pair, gated by a syllable envelope.
        float env = smoothstepf(0.0f, 0.1f, p) * (1.0f - smoothstepf(0.6f, 1.0f, p));
        v.ph3 += 3.7f * dt;
        float syll = 0.5f + 0.5f * std::sin(v.ph3 * TAU * 1.7f);
        syll = syll * syll;
        float c1 = 0.06f + 0.03f * std::sin(v.ph3 * TAU * 0.9f);
        v.lp1 += (n - v.lp1) * c1;
        v.lp2 += (v.lp1 - v.lp2) * c1;
        float form = (v.lp1 - v.lp2) * 6.0f;
        v.bp1 += (n - v.bp1) * 0.22f;
        v.bp2 += (v.bp1 - v.bp2) * 0.22f;
        float hiss = (v.bp1 - v.bp2) * 2.0f;
        s = (form * 0.7f + hiss * 0.3f) * syll * env * 0.5f;
        break;
    }
    case SND_LIGHT_POP: {
        float crack = n * std::exp(-t * 55.0f);
        v.ph1 += 100.0f * dt;                      // mains hum dying out
        float hum = std::sin(v.ph1 * TAU) * std::exp(-t * 9.0f) * 0.3f;
        s = (crack * 0.8f + hum) * 0.7f;
        break;
    }
    case SND_EXIT_OPEN: {
        float env = smoothstepf(0.0f, 0.2f, p) * (1.0f - smoothstepf(0.8f, 1.0f, p));
        float f = 48.0f + p * 40.0f;
        v.ph1 += f * dt;
        v.lp1 += (n - v.lp1) * 0.04f;
        float servo = v.lp1 * 2.0f;
        s = (std::sin(v.ph1 * TAU) * 0.5f + servo * 0.5f) * env * 0.6f;
        break;
    }
    case SND_DEATH: {
        // Impact slam, then a screech that collapses into a flat sine.
        float slam = n * std::exp(-t * 12.0f) * 1.2f;
        float f = 700.0f * std::exp(-t * 1.1f) + 90.0f;
        v.ph1 += f * dt;
        v.ph2 += f * 0.5013f * dt;
        float scream = (std::sin(v.ph1 * TAU) + std::sin(v.ph2 * TAU) * 0.7f)
                       * std::exp(-t * 0.9f);
        float flat = (p > 0.6f) ? std::sin(v.ph3 * TAU) * (p - 0.6f) * 0.8f : 0.0f;
        v.ph3 += 1000.0f * dt;
        s = std::tanh(slam + scream * 0.8f) * 0.7f + flat * 0.25f;
        break;
    }
    case SND_HEARTBEAT_THUMP: {
        float env = std::exp(-t * 15.0f);
        float f = 52.0f * v.pitch * (1.0f - 0.35f * p);
        v.ph1 += f * dt;
        s = std::tanh(std::sin(v.ph1 * TAU) * 1.8f) * env * 0.9f;
        break;
    }
    case SND_BREATH: {
        float env = std::sin(p * PI);
        float cut = 0.10f + 0.10f * env;
        v.lp1 += (n - v.lp1) * cut;
        v.lp2 += (v.lp1 - v.lp2) * cut;
        s = (v.lp1 - v.lp2) * 5.0f * env * 0.5f;
        break;
    }
    default:
        break;
    }

    s *= v.gain;
    sendOut = kSend[v.id];
    return s;
}

// Baked-sample playback with linear interpolation. Sample rates differ from
// the mixer rate, so the read cursor advances fractionally.
float AudioEngine::renderSampleVoice(Voice& v, float dt, float& sendOut) {
    if (v.sample < 0 || v.sample >= (int)mSamples.size()) { v.active = false; return 0.0f; }
    const Sample& sm = mSamples[v.sample];
    double step = (double)sm.rate / SAMPLE_RATE * v.pitch;
    double c = v.cursor;
    v.cursor += step;
    if (c >= sm.frames - 1) { v.active = false; return 0.0f; }

    uint32_t i0 = (uint32_t)c;
    uint32_t i1 = i0 + 1;
    float frac = (float)(c - i0);
    float s;
    if (sm.stereo) {
        float a = sm.pcm[i0 * 2] * (1.0f / 32768.0f);
        float b = sm.pcm[i1 * 2] * (1.0f / 32768.0f);
        s = lerpf(a, b, frac);
    } else {
        float a = sm.pcm[i0] * (1.0f / 32768.0f);
        float b = sm.pcm[i1] * (1.0f / 32768.0f);
        s = lerpf(a, b, frac);
    }
    (void)dt;
    sendOut = v.dialogue ? 0.05f : 0.45f;
    return s * v.gain;
}

// --------------------------------------------------------------------- reverb

float AudioEngine::reverb(float in, float indoor) {
    float acc = 0.0f;
    for (int i = 0; i < NCOMB; i++) {
        std::vector<float>& buf = mComb[i];
        int& idx = mCombIdx[i];
        float out = buf[idx];
        // One-pole damping in the feedback path: high frequencies die first,
        // exactly like they do in a concrete corridor.
        // Indoors the tail is shorter and darker; outdoors a street opens up
        // into a longer, brighter reflection.
        float damp = lerpf(0.30f, 0.52f, indoor);
        float fb = mCombFb[i] * lerpf(1.0f, 0.86f, indoor);
        mCombLp[i] += (out - mCombLp[i]) * damp;
        buf[idx] = in + mCombLp[i] * fb;
        idx = (idx + 1) % (int)buf.size();
        acc += out;
    }
    acc *= 0.25f;
    for (int i = 0; i < NAP; i++) {
        std::vector<float>& buf = mAp[i];
        int& idx = mApIdx[i];
        float bufOut = buf[idx];
        float out = -acc + bufOut;
        buf[idx] = acc + bufOut * 0.5f;
        idx = (idx + 1) % (int)buf.size();
        acc = out;
    }
    return acc;
}

// ------------------------------------------------------------------- mix loop

void AudioEngine::renderBlock(int16_t* out, int frames) {
    // Drain queued events.
    uint32_t r = mRead.load(std::memory_order_relaxed);
    uint32_t w = mWrite.load(std::memory_order_acquire);
    while (r != w) {
        triggerVoice(mRing[r % RING_SIZE]);
        r++;
    }
    mRead.store(r, std::memory_order_release);

    const float dt = kInvSR;
    vec3 lp(mLx.load(), mLy.load(), mLz.load());
    vec3 fwd(mFx.load(), mFy.load(), mFz.load());
    vec3 right = normalize(cross(fwd, vec3(0, 1, 0)));
    float tension = mTension.load();
    float heartBpm = mHeartBpm.load();
    float breath = mBreath.load();
    float muffle = mMuffle.load();
    float master = mMaster.load();
    float indoor = mIndoor.load();

    // Crossfade to a new ambience bed when the game asks for one.
    int wantBed = mBedTarget.load();
    if (wantBed != mBedCur) {
        mBedPrev = mBedCur;
        mBedPrevCursor = mBedCursor;
        mBedCur = wantBed;
        mBedCursor = 0.0;
        mBedFade = 0.0f;
    }

    bool anyDialogue = false;

    for (int i = 0; i < frames; i++) {
        float dryL = 0.0f, dryR = 0.0f, send = 0.0f;

        for (int vi = 0; vi < MAX_VOICES; vi++) {
            Voice& v = mVoices[vi];
            if (!v.active) continue;
            float sd = 0.0f;
            float s = (v.sample >= 0) ? renderSampleVoice(v, dt, sd)
                                      : renderVoice(v, dt, sd);
            if (v.dialogue && v.active) anyDialogue = true;
            v.t += dt;
            if (v.t > v.dur) v.active = false;

            float gl = 1.0f, gr = 1.0f, att = 1.0f;
            if (v.positional) {
                vec3 d = v.pos - lp;
                float dist = length(d);
                att = 1.0f / (1.0f + 0.30f * dist);
                if (dist > 40.0f) att = 0.0f;
                float pan = dist > 0.01f ? clampf(dot(normalize(d), right), -1.0f, 1.0f) : 0.0f;
                // Constant-power pan.
                float a = (pan + 1.0f) * 0.25f * PI;
                gl = std::cos(a);
                gr = std::sin(a);
            } else {
                gl = gr = 0.7071f;
            }
            float sv = s * att;
            dryL += sv * gl;
            dryR += sv * gr;
            send += sv * sd;
        }

        // --- baked ambience bed ---
        float bedL = 0.0f, bedR = 0.0f;
        auto readBed = [&](int idx, double& cursor, float& l, float& r) {
            if (idx < 0 || idx >= (int)mSamples.size()) return;
            const Sample& sm = mSamples[idx];
            if (sm.frames < 2) return;
            double step = (double)sm.rate / SAMPLE_RATE;
            if (cursor >= sm.frames - 1) cursor = 0.0;   // beds loop
            uint32_t i0 = (uint32_t)cursor;
            uint32_t i1 = i0 + 1;
            float f = (float)(cursor - i0);
            if (sm.stereo) {
                l = lerpf(sm.pcm[i0 * 2] * (1.0f / 32768.0f), sm.pcm[i1 * 2] * (1.0f / 32768.0f), f);
                r = lerpf(sm.pcm[i0 * 2 + 1] * (1.0f / 32768.0f), sm.pcm[i1 * 2 + 1] * (1.0f / 32768.0f), f);
            } else {
                l = r = lerpf(sm.pcm[i0] * (1.0f / 32768.0f), sm.pcm[i1] * (1.0f / 32768.0f), f);
            }
            cursor += step;
        };
        {
            float cl = 0, cr = 0, pl = 0, pr = 0;
            readBed(mBedCur, mBedCursor, cl, cr);
            if (mBedFade < 1.0f) readBed(mBedPrev, mBedPrevCursor, pl, pr);
            bedL = lerpf(pl, cl, mBedFade);
            bedR = lerpf(pr, cr, mBedFade);
            mBedFade = std::min(1.0f, mBedFade + dt * 0.5f);
        }
        // Dialogue ducks the bed so a radio call is never fighting the wind.
        mDuck = lerpf(mDuck, anyDialogue ? 0.34f : 1.0f, clampf(dt * 3.5f, 0.0f, 1.0f));
        bedL *= mDuck;
        bedR *= mDuck;

        // --- synthesised ambient layer ---
        // Heartbeat: a lub-dub pair whose rate and depth follow tension.
        mHeartPhase += (heartBpm / 60.0f) * dt;
        if (mHeartPhase >= 1.0f) mHeartPhase -= 1.0f;
        float hb = 0.0f;
        {
            float hp = mHeartPhase;
            float lub = hp < 0.12f ? std::exp(-hp * 42.0f) : 0.0f;
            float dub = (hp > 0.16f && hp < 0.30f) ? std::exp(-(hp - 0.16f) * 46.0f) * 0.65f : 0.0f;
            float hf = 46.0f + tension * 10.0f;
            mDronePh3 += hf * dt;
            hb = std::tanh(std::sin(mDronePh3 * TAU) * 2.0f) * (lub + dub);
            hb *= (0.10f + 0.55f * tension);
        }

        // Breathing: gets audible when you sprint or when tension is high.
        mBreathPhase += dt * (0.32f + 0.55f * breath);
        if (mBreathPhase >= 1.0f) mBreathPhase -= 1.0f;
        float br = 0.0f;
        if (breath > 0.01f) {
            float be = std::sin(mBreathPhase * TAU);
            float cut = 0.09f + 0.08f * std::fabs(be);
            float nn = noise(mRng);
            mNoiseLp += (nn - mNoiseLp) * cut;
            mNoiseLp2 += (mNoiseLp - mNoiseLp2) * cut;
            br = (mNoiseLp - mNoiseLp2) * 5.0f * std::fabs(be) * breath * 0.35f;
        }

        // Drone: three detuned sub sines that beat against each other, plus a
        // filtered rumble. Volume tracks tension, so the room itself gets tighter.
        mDronePh1 += 41.0f * dt;
        mDronePh2 += 41.0f * 1.0067f * dt;
        float drone = (std::sin(mDronePh1 * TAU) + std::sin(mDronePh2 * TAU) * 0.8f)
                      * (0.020f + 0.085f * tension);
        float rn = noise(mRng);
        mRumbleLp += (rn - mRumbleLp) * 0.0025f;
        float rumble = mRumbleLp * (2.2f + 5.0f * tension);

        float bed = hb + br + drone + rumble * 0.35f;
        dryL += bed + bedL * 0.85f;
        dryR += bed + bedR * 0.85f;
        send += (drone + rumble) * 0.5f;

        // --- reverb ---
        float rv = reverb(send * 0.5f, indoor);
        float wet = lerpf(0.42f, 0.30f, indoor);
        float outL = dryL + rv * wet;
        float outR = dryR + rv * (wet - 0.04f);

        // Muffle: used on death and when the creature is on top of you, so the
        // world drops away and only the heartbeat is left.
        if (muffle > 0.001f) {
            float c = lerpf(1.0f, 0.045f, muffle);
            mMuffleLpL += (outL - mMuffleLpL) * c;
            mMuffleLpR += (outR - mMuffleLpR) * c;
            outL = lerpf(outL, mMuffleLpL, muffle);
            outR = lerpf(outR, mMuffleLpR, muffle);
        }

        outL = std::tanh(outL * master * 0.9f);
        outR = std::tanh(outR * master * 0.9f);

        out[i * 2 + 0] = (int16_t)(clampf(outL, -1.0f, 1.0f) * 32000.0f);
        out[i * 2 + 1] = (int16_t)(clampf(outR, -1.0f, 1.0f) * 32000.0f);
    }

    mVoiceBusy.store(anyDialogue ? 1.0f : 0.0f);
}

// ------------------------------------------------------------------- OpenSL ES

#if defined(HOLLOW_NO_OPENSL)

// Host build: no audio device. The reverb lines and mix buffers still get
// allocated so renderBlock() behaves exactly as it does on a phone.
bool AudioEngine::start() {
    if (mRunning) return true;
    buildSampleTable();
    const int combLen[NCOMB] = {1687, 1601, 2053, 2251};
    const int apLen[NAP] = {389, 127};
    for (int i = 0; i < NCOMB; i++) mComb[i].assign(combLen[i], 0.0f);
    for (int i = 0; i < NAP; i++) mAp[i].assign(apLen[i], 0.0f);
    std::memset(mBuf, 0, sizeof(mBuf));
    mRunning = true;
    return true;
}
void AudioEngine::stop() { mRunning = false; }
void AudioEngine::enqueueNext() {
    if (!mRunning) return;
    renderBlock(mBuf[mCurBuf], BLOCK_FRAMES);
    mCurBuf ^= 1;
}

#else

static void bqCallback(SLAndroidSimpleBufferQueueItf, void* ctx) {
    static_cast<AudioEngine*>(ctx)->enqueueNext();
}

void AudioEngine::enqueueNext() {
    if (!mRunning) return;
    int16_t* buf = mBuf[mCurBuf];
    renderBlock(buf, BLOCK_FRAMES);
    SLAndroidSimpleBufferQueueItf bq = (SLAndroidSimpleBufferQueueItf)mBufferQueue;
    (*bq)->Enqueue(bq, buf, BLOCK_FRAMES * 2 * sizeof(int16_t));
    mCurBuf ^= 1;
}

bool AudioEngine::start() {
    if (mRunning) return true;
    buildSampleTable();

    // Reverb delay lines - mutually prime lengths avoid a metallic ring.
    const int combLen[NCOMB] = {1687, 1601, 2053, 2251};
    const int apLen[NAP] = {389, 127};
    for (int i = 0; i < NCOMB; i++) mComb[i].assign(combLen[i], 0.0f);
    for (int i = 0; i < NAP; i++) mAp[i].assign(apLen[i], 0.0f);
    std::memset(mBuf, 0, sizeof(mBuf));

    SLObjectItf engineObj = nullptr;
    if (slCreateEngine(&engineObj, 0, nullptr, 0, nullptr, nullptr) != SL_RESULT_SUCCESS) return false;
    if ((*engineObj)->Realize(engineObj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) return false;
    SLEngineItf engine = nullptr;
    if ((*engineObj)->GetInterface(engineObj, SL_IID_ENGINE, &engine) != SL_RESULT_SUCCESS) return false;

    SLObjectItf mixObj = nullptr;
    if ((*engine)->CreateOutputMix(engine, &mixObj, 0, nullptr, nullptr) != SL_RESULT_SUCCESS) return false;
    if ((*mixObj)->Realize(mixObj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) return false;

    SLDataLocator_AndroidSimpleBufferQueue locBq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
    SLDataFormat_PCM fmt = {
        SL_DATAFORMAT_PCM, 2, SL_SAMPLINGRATE_44_1,
        SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT, SL_BYTEORDER_LITTLEENDIAN
    };
    SLDataSource src = {&locBq, &fmt};
    SLDataLocator_OutputMix locMix = {SL_DATALOCATOR_OUTPUTMIX, mixObj};
    SLDataSink sink = {&locMix, nullptr};

    const SLInterfaceID ids[1] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean req[1] = {SL_BOOLEAN_TRUE};
    SLObjectItf playerObj = nullptr;
    if ((*engine)->CreateAudioPlayer(engine, &playerObj, &src, &sink, 1, ids, req) != SL_RESULT_SUCCESS) return false;
    if ((*playerObj)->Realize(playerObj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) return false;

    SLPlayItf play = nullptr;
    SLAndroidSimpleBufferQueueItf bq = nullptr;
    if ((*playerObj)->GetInterface(playerObj, SL_IID_PLAY, &play) != SL_RESULT_SUCCESS) return false;
    if ((*playerObj)->GetInterface(playerObj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &bq) != SL_RESULT_SUCCESS) return false;
    if ((*bq)->RegisterCallback(bq, bqCallback, this) != SL_RESULT_SUCCESS) return false;

    mEngineObj = (void*)engineObj; mEngineItf = (void*)engine;
    mOutputMix = (void*)mixObj; mPlayerObj = (void*)playerObj;
    mPlayerPlay = (void*)play; mBufferQueue = (void*)bq;
    mRunning = true;

    if ((*play)->SetPlayState(play, SL_PLAYSTATE_PLAYING) != SL_RESULT_SUCCESS) {
        mRunning = false;
        return false;
    }
    // Prime both buffers so playback starts without a gap.
    enqueueNext();
    enqueueNext();
    ALOG("audio started (44.1kHz stereo, %d-frame blocks)", BLOCK_FRAMES);
    return true;
}

void AudioEngine::stop() {
    if (!mRunning) return;
    mRunning = false;
    if (mPlayerPlay) {
        SLPlayItf play = (SLPlayItf)mPlayerPlay;
        (*play)->SetPlayState(play, SL_PLAYSTATE_STOPPED);
    }
    if (mPlayerObj) { SLObjectItf o = (SLObjectItf)mPlayerObj; (*o)->Destroy(o); mPlayerObj = nullptr; }
    if (mOutputMix) { SLObjectItf o = (SLObjectItf)mOutputMix; (*o)->Destroy(o); mOutputMix = nullptr; }
    if (mEngineObj) { SLObjectItf o = (SLObjectItf)mEngineObj; (*o)->Destroy(o); mEngineObj = nullptr; }
    mPlayerPlay = mBufferQueue = mEngineItf = nullptr;
}

#endif // HOLLOW_NO_OPENSL

} // namespace hm
