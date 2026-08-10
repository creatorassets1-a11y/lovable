// audio.h - real-time procedural audio. Every sound in this game is synthesised
// sample-by-sample on the audio thread: no .wav files, no licensing, no bloat.
//
// Threading contract: the game thread only ever calls post()/set*(); those write
// into a lock-free SPSC ring or into atomics. The audio thread only reads.
#pragma once
#include "hmath.h"
#include <atomic>
#include <vector>

namespace hm {

enum SoundId : int {
    SND_FOOTSTEP = 0,
    SND_FOOTSTEP_WET,
    SND_CREATURE_STEP,
    SND_STINGER,
    SND_SCREECH,
    SND_GROWL,
    SND_PICKUP,
    SND_DOOR,
    SND_FLASHLIGHT,
    SND_DRIP,
    SND_METAL,
    SND_WHISPER,
    SND_LIGHT_POP,
    SND_EXIT_OPEN,
    SND_DEATH,
    SND_HEARTBEAT_THUMP,
    SND_BREATH,
    SND_COUNT
};

struct SoundEvent {
    int id;
    float x, y, z;
    float gain;
    float pitch;
};

class AudioEngine {
public:
    bool start();
    void stop();

    // --- called from the game thread only ---
    void post(SoundId id, const vec3& pos, float gain = 1.0f, float pitch = 1.0f);
    void postUI(SoundId id, float gain = 1.0f, float pitch = 1.0f);  // non-positional
    void setListener(const vec3& pos, const vec3& forward);
    void setTension(float t)   { mTension.store(clampf(t, 0.0f, 1.0f)); }
    void setHeartRate(float b) { mHeartBpm.store(clampf(b, 40.0f, 220.0f)); }
    void setBreath(float b)    { mBreath.store(clampf(b, 0.0f, 1.0f)); }
    void setMuffle(float m)    { mMuffle.store(clampf(m, 0.0f, 1.0f)); }
    void setMasterGain(float g){ mMaster.store(clampf(g, 0.0f, 1.0f)); }

    // --- audio thread ---
    void renderBlock(int16_t* out, int frames);

    static const int SAMPLE_RATE = 44100;

private:
    struct Voice {
        bool  active = false;
        int   id = 0;
        float t = 0.0f;          // seconds since trigger
        float dur = 0.0f;
        float gain = 1.0f;
        float pitch = 1.0f;
        vec3  pos;
        bool  positional = true;
        // per-voice DSP state
        float ph1 = 0, ph2 = 0, ph3 = 0;
        float lp1 = 0, lp2 = 0, bp1 = 0, bp2 = 0;
        uint32_t rng = 0x2545F491u;
    };

    static const int MAX_VOICES = 28;
    static const int RING_SIZE = 64;

    Voice mVoices[MAX_VOICES];
    SoundEvent mRing[RING_SIZE];
    std::atomic<uint32_t> mWrite{0};
    std::atomic<uint32_t> mRead{0};

    std::atomic<float> mLx{0}, mLy{0}, mLz{0}, mFx{0}, mFy{0}, mFz{1};
    std::atomic<float> mTension{0}, mHeartBpm{62}, mBreath{0}, mMuffle{0}, mMaster{1};

    // ambience / bed state (audio thread only)
    float mHeartPhase = 0.0f;
    float mBreathPhase = 0.0f;
    float mDronePh1 = 0, mDronePh2 = 0, mDronePh3 = 0;
    float mNoiseLp = 0, mNoiseLp2 = 0;
    float mRumbleLp = 0;
    uint32_t mRng = 0x9E3779B9u;
    float mMuffleLpL = 0, mMuffleLpR = 0;

    // Schroeder reverb - four combs into two allpasses. This is what makes the
    // corridors sound like concrete instead of like a phone speaker.
    static const int NCOMB = 4;
    static const int NAP = 2;
    std::vector<float> mComb[NCOMB];
    int mCombIdx[NCOMB] = {0, 0, 0, 0};
    float mCombFb[NCOMB] = {0.805f, 0.827f, 0.783f, 0.764f};
    float mCombLp[NCOMB] = {0, 0, 0, 0};
    std::vector<float> mAp[NAP];
    int mApIdx[NAP] = {0, 0};

    float noise(uint32_t& s) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return ((int32_t)(s >> 8) * (1.0f / 8388608.0f)) - 1.0f;
    }
    void triggerVoice(const SoundEvent& e);
    float renderVoice(Voice& v, float dt, float& sendOut);
    float reverb(float in);

    void* mEngineObj = nullptr;
    void* mEngineItf = nullptr;
    void* mOutputMix = nullptr;
    void* mPlayerObj = nullptr;
    void* mPlayerPlay = nullptr;
    void* mBufferQueue = nullptr;
    bool  mRunning = false;

public:
    // Double-buffered PCM handed to OpenSL; public so the static C callback can reach it.
    static const int BLOCK_FRAMES = 512;
    int16_t mBuf[2][BLOCK_FRAMES * 2];
    int mCurBuf = 0;
    void enqueueNext();
};

} // namespace hm
