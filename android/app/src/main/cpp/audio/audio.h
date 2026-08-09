// Audio engine.
//
// A software mixer feeding AAudio directly. Everything is mixed in C++ so the
// game can position sounds in 3D, duck under dialogue, and low-pass the world
// when the player is hiding — none of which SoundPool can do.
//
// Sounds are split by role, exactly as memory demands:
//   ONE-SHOT  short cues, decoded to PCM at load (footsteps, stingers, screams)
//   STREAM    long beds and dialogue, decoded incrementally off the mixer thread
#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/hmath.h"

struct AAudioStreamStruct;
typedef struct AAudioStreamStruct AAudioStream;

namespace hl {

using namespace hm;

constexpr int MIX_RATE = 22050;   // every asset is authored at this rate
constexpr int MAX_VOICES = 24;

struct Clip {
    std::vector<float> pcm;   // interleaved stereo, or mono if channels == 1
    int channels = 1;
    int frames = 0;
};

enum class Bus { Sfx, Voice, Ambient, Music, Count };

struct PlayParams {
    float gain = 1.0f;
    float pitch = 1.0f;
    float pan = 0.0f;          // -1..1, ignored when positional
    bool loop = false;
    Bus bus = Bus::Sfx;
    bool positional = false;
    v3 pos;
    float range = 22.0f;
    /** Extra low-pass, 0 = open. Used for sounds heard through a wall. */
    float occlusion = 0.0f;
};

class AudioEngine {
public:
    AudioEngine();
    // Out-of-line: Voice is only complete inside audio.cpp.
    ~AudioEngine();

    bool start();
    void stop();
    void pause();
    void resume();

    /** Decode an OGG into memory. Returns false if the asset is missing. */
    bool load_clip(const std::string& name, const std::string& path);
    bool has_clip(const std::string& name) const;

    /** Returns a voice handle, or -1 if nothing was free. */
    int play(const std::string& name, const PlayParams& p = {});
    void stop_voice(int handle, float fade = 0.05f);
    void set_voice_gain(int handle, float gain);
    void set_voice_pos(int handle, v3 pos);
    bool voice_active(int handle) const;

    /** Dialogue: one line at a time, ducking the other buses under it. */
    int speak(const std::string& name, float gain = 1.0f);
    void stop_speech();
    bool speaking() const;

    /** Ambient bed cross-faded in and out by the chapter. */
    void set_bed(const std::string& name, float gain, float fade = 2.0f);

    void set_listener(v3 pos, v3 forward, v3 up);
    void set_bus_gain(Bus b, float gain);
    void set_master(float gain);
    /** 0 = clear, 1 = heavily muffled (inside a locker, or blacking out). */
    void set_muffle(float amount);

    int active_voices() const;

private:
    struct Voice;
    void mix(float* out, int frames);
    static int32_t data_callback(AAudioStream*, void*, void*, int32_t);

    AAudioStream* stream_ = nullptr;
    std::unordered_map<std::string, std::shared_ptr<Clip>> clips_;
    std::vector<Voice> voices_;
    mutable std::mutex lock_;

    v3 listener_pos_, listener_fwd_{0, 0, 1}, listener_right_{1, 0, 0};
    float bus_gain_[(int)Bus::Count] = {1.0f, 1.0f, 0.9f, 0.8f};
    float master_ = 1.0f;
    float muffle_ = 0.0f, muffle_state_l_ = 0.0f, muffle_state_r_ = 0.0f;
    float duck_ = 1.0f;
    int speech_voice_ = -1;
    std::string bed_name_;
    int bed_voice_ = -1;
    std::atomic<int> active_{0};
    bool running_ = false;
};

/** Decode an OGG Vorbis file from memory. Exposed for tooling/tests. */
bool decode_ogg(const std::vector<uint8_t>& bytes, Clip* out);

}  // namespace hl
