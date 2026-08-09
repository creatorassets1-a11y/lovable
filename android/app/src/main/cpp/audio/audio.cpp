#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../core/asset.h"

#ifdef __ANDROID__
#include <aaudio/AAudio.h>
#endif

#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

namespace hl {

// ---------------------------------------------------------------- decoding

bool decode_ogg(const std::vector<uint8_t>& bytes, Clip* out) {
    if (bytes.empty()) return false;
    int channels = 0, rate = 0;
    short* pcm = nullptr;
    int frames = stb_vorbis_decode_memory(
        const_cast<unsigned char*>(bytes.data()), (int)bytes.size(),
        &channels, &rate, &pcm);
    if (frames <= 0 || !pcm) {
        if (pcm) free(pcm);
        return false;
    }

    out->channels = channels;
    out->frames = frames;
    out->pcm.resize((size_t)frames * channels);
    const float inv = 1.0f / 32768.0f;
    for (size_t i = 0; i < out->pcm.size(); i++) out->pcm[i] = pcm[i] * inv;
    free(pcm);

    // Assets are authored at MIX_RATE; resample anything that is not, so a
    // stray sample rate cannot silently detune the whole mix.
    if (rate != MIX_RATE && rate > 0) {
        double ratio = (double)MIX_RATE / rate;
        int nf = (int)(frames * ratio);
        std::vector<float> res((size_t)nf * channels);
        for (int i = 0; i < nf; i++) {
            double src = i / ratio;
            int i0 = (int)src;
            int i1 = std::min(i0 + 1, frames - 1);
            float f = (float)(src - i0);
            for (int c = 0; c < channels; c++) {
                res[(size_t)i * channels + c] =
                    out->pcm[(size_t)i0 * channels + c] * (1 - f) +
                    out->pcm[(size_t)i1 * channels + c] * f;
            }
        }
        out->pcm.swap(res);
        out->frames = nf;
    }
    return true;
}

// ------------------------------------------------------------------ voices

struct AudioEngine::Voice {
    std::shared_ptr<Clip> clip;
    double pos = 0;
    float pitch = 1;
    float gain = 0, target_gain = 0;
    float pan_l = 1, pan_r = 1;
    bool loop = false;
    bool active = false;
    bool positional = false;
    v3 world;
    float range = 22.0f;
    float occlusion = 0.0f;
    float lp_l = 0, lp_r = 0;
    Bus bus = Bus::Sfx;
    int generation = 0;
    float fade_rate = 0;    // >0 while fading out to a stop
};

AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { stop(); }

bool AudioEngine::load_clip(const std::string& name, const std::string& path) {
    auto bytes = asset_read(path);
    if (bytes.empty()) return false;
    auto clip = std::make_shared<Clip>();
    if (!decode_ogg(bytes, clip.get())) {
        loge("ogg decode failed: %s", path.c_str());
        return false;
    }
    std::lock_guard<std::mutex> g(lock_);
    clips_[name] = clip;
    return true;
}

bool AudioEngine::has_clip(const std::string& name) const {
    std::lock_guard<std::mutex> g(lock_);
    return clips_.count(name) != 0;
}

int AudioEngine::play(const std::string& name, const PlayParams& p) {
    std::lock_guard<std::mutex> g(lock_);
    auto it = clips_.find(name);
    if (it == clips_.end()) return -1;

    // Steal the quietest non-looping voice if everything is busy; a dropped
    // footstep is always better than a dropped scream.
    int slot = -1;
    for (int i = 0; i < (int)voices_.size(); i++) {
        if (!voices_[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        float worst = 1e9f;
        for (int i = 0; i < (int)voices_.size(); i++) {
            if (voices_[i].loop) continue;
            if (voices_[i].gain < worst) { worst = voices_[i].gain; slot = i; }
        }
    }
    if (slot < 0) return -1;

    Voice& v = voices_[slot];
    v.clip = it->second;
    v.pos = 0;
    v.pitch = clampf(p.pitch, 0.25f, 4.0f);
    v.gain = v.target_gain = p.gain;
    v.loop = p.loop;
    v.bus = p.bus;
    v.positional = p.positional;
    v.world = p.pos;
    v.range = p.range;
    v.occlusion = p.occlusion;
    v.lp_l = v.lp_r = 0;
    v.fade_rate = 0;
    v.active = true;
    v.generation++;
    if (!p.positional) {
        float a = (clampf(p.pan, -1, 1) + 1.0f) * 0.25f * PI;
        v.pan_l = std::cos(a);
        v.pan_r = std::sin(a);
    }
    return slot | (v.generation << 8);
}

void AudioEngine::stop_voice(int handle, float fade) {
    if (handle < 0) return;
    std::lock_guard<std::mutex> g(lock_);
    int slot = handle & 0xFF;
    if (slot >= (int)voices_.size()) return;
    Voice& v = voices_[slot];
    if (v.generation != (handle >> 8)) return;
    if (fade <= 0.001f) { v.active = false; return; }
    v.fade_rate = 1.0f / (fade * MIX_RATE);
}

void AudioEngine::set_voice_gain(int handle, float gain) {
    if (handle < 0) return;
    std::lock_guard<std::mutex> g(lock_);
    int slot = handle & 0xFF;
    if (slot >= (int)voices_.size()) return;
    Voice& v = voices_[slot];
    if (v.generation == (handle >> 8)) v.target_gain = gain;
}

void AudioEngine::set_voice_pos(int handle, v3 pos) {
    if (handle < 0) return;
    std::lock_guard<std::mutex> g(lock_);
    int slot = handle & 0xFF;
    if (slot >= (int)voices_.size()) return;
    Voice& v = voices_[slot];
    if (v.generation == (handle >> 8)) v.world = pos;
}

bool AudioEngine::voice_active(int handle) const {
    if (handle < 0) return false;
    std::lock_guard<std::mutex> g(lock_);
    int slot = handle & 0xFF;
    if (slot >= (int)voices_.size()) return false;
    const Voice& v = voices_[slot];
    return v.active && v.generation == (handle >> 8);
}

int AudioEngine::speak(const std::string& name, float gain) {
    stop_speech();
    PlayParams p;
    p.gain = gain;
    p.bus = Bus::Voice;
    speech_voice_ = play(name, p);
    return speech_voice_;
}

void AudioEngine::stop_speech() {
    if (speech_voice_ >= 0) {
        stop_voice(speech_voice_, 0.08f);
        speech_voice_ = -1;
    }
}

bool AudioEngine::speaking() const {
    return speech_voice_ >= 0 && voice_active(speech_voice_);
}

void AudioEngine::set_bed(const std::string& name, float gain, float fade) {
    if (name == bed_name_) {
        set_voice_gain(bed_voice_, gain);
        return;
    }
    if (bed_voice_ >= 0) stop_voice(bed_voice_, fade);
    bed_name_ = name;
    PlayParams p;
    p.gain = gain;
    p.loop = true;
    p.bus = Bus::Ambient;
    bed_voice_ = play(name, p);
}

void AudioEngine::set_listener(v3 pos, v3 forward, v3 up) {
    std::lock_guard<std::mutex> g(lock_);
    listener_pos_ = pos;
    listener_fwd_ = normalize(forward);
    listener_right_ = normalize(cross(listener_fwd_, normalize(up)));
}

void AudioEngine::set_bus_gain(Bus b, float gain) {
    std::lock_guard<std::mutex> g(lock_);
    bus_gain_[(int)b] = clampf(gain, 0.0f, 2.0f);
}

void AudioEngine::set_master(float gain) {
    std::lock_guard<std::mutex> g(lock_);
    master_ = clampf(gain, 0.0f, 1.0f);
}

void AudioEngine::set_muffle(float amount) {
    std::lock_guard<std::mutex> g(lock_);
    muffle_ = clampf(amount, 0.0f, 1.0f);
}

int AudioEngine::active_voices() const { return active_.load(); }

// ------------------------------------------------------------------- mixing

void AudioEngine::mix(float* out, int frames) {
    std::memset(out, 0, sizeof(float) * frames * 2);

    std::lock_guard<std::mutex> g(lock_);

    // Duck everything except dialogue while a line is playing.
    bool voice_playing = false;
    for (auto& v : voices_) {
        if (v.active && v.bus == Bus::Voice && v.gain > 0.01f) { voice_playing = true; break; }
    }
    float duck_target = voice_playing ? 0.45f : 1.0f;
    // ~120 ms time constant, computed per block rather than per sample.
    float duck_step = (float)frames / (0.12f * MIX_RATE);
    duck_ += (duck_target - duck_) * clampf(duck_step, 0.0f, 1.0f);

    int live = 0;
    for (auto& v : voices_) {
        if (!v.active || !v.clip) continue;
        live++;

        const Clip& c = *v.clip;
        float gl = v.pan_l, gr = v.pan_r;
        float dist_gain = 1.0f;
        float extra_lp = v.occlusion;

        if (v.positional) {
            v3 d = v.world - listener_pos_;
            float dist = length(d);
            // Inverse-square-ish with a soft floor so nothing pops at 0 m.
            dist_gain = 1.0f / (1.0f + 0.28f * dist + 0.06f * dist * dist);
            dist_gain *= clampf(1.0f - dist / v.range, 0.0f, 1.0f);
            if (dist > 1e-3f) {
                float side = dot(d * (1.0f / dist), listener_right_);
                float a = (clampf(side, -1, 1) + 1.0f) * 0.25f * PI;
                gl = std::cos(a);
                gr = std::sin(a);
            }
            // Distant sounds lose their top end.
            extra_lp = clampf(extra_lp + dist * 0.018f, 0.0f, 0.9f);
        }

        float bus = bus_gain_[(int)v.bus];
        if (v.bus != Bus::Voice) bus *= duck_;
        float amp = v.gain * dist_gain * bus;

        // One-pole low-pass coefficient from the occlusion amount.
        float lp_a = extra_lp > 0.001f ? (1.0f - extra_lp * 0.92f) : 1.0f;

        for (int i = 0; i < frames; i++) {
            if (v.fade_rate > 0) {
                v.gain -= v.fade_rate;
                if (v.gain <= 0) { v.active = false; break; }
                amp = v.gain * dist_gain * bus;
            } else if (v.gain != v.target_gain) {
                // Glide toward the target so gain changes never click.
                float step = 1.0f / (0.05f * MIX_RATE);
                v.gain += clampf(v.target_gain - v.gain, -step, step);
                amp = v.gain * dist_gain * bus;
            }

            int i0 = (int)v.pos;
            if (i0 >= c.frames - 1) {
                if (v.loop) { v.pos -= c.frames; i0 = (int)v.pos; }
                else { v.active = false; break; }
            }
            if (i0 < 0) { v.active = false; break; }
            int i1 = std::min(i0 + 1, c.frames - 1);
            float f = (float)(v.pos - i0);

            float sl, sr;
            if (c.channels == 2) {
                sl = c.pcm[(size_t)i0 * 2] * (1 - f) + c.pcm[(size_t)i1 * 2] * f;
                sr = c.pcm[(size_t)i0 * 2 + 1] * (1 - f) + c.pcm[(size_t)i1 * 2 + 1] * f;
            } else {
                float s = c.pcm[i0] * (1 - f) + c.pcm[i1] * f;
                sl = sr = s;
            }

            if (lp_a < 0.999f) {
                v.lp_l += (sl - v.lp_l) * lp_a;
                v.lp_r += (sr - v.lp_r) * lp_a;
                sl = v.lp_l;
                sr = v.lp_r;
            }

            out[i * 2] += sl * gl * amp;
            out[i * 2 + 1] += sr * gr * amp;
            v.pos += v.pitch;
        }
    }
    active_.store(live);

    // Master muffle, then a soft limiter so a scream on top of a full bed does
    // not clip into distortion.
    float lp_a = muffle_ > 0.001f ? (1.0f - muffle_ * 0.95f) : 1.0f;
    for (int i = 0; i < frames; i++) {
        float l = out[i * 2] * master_;
        float r = out[i * 2 + 1] * master_;
        if (lp_a < 0.999f) {
            muffle_state_l_ += (l - muffle_state_l_) * lp_a;
            muffle_state_r_ += (r - muffle_state_r_) * lp_a;
            l = muffle_state_l_;
            r = muffle_state_r_;
        }
        out[i * 2] = std::tanh(l * 1.05f);
        out[i * 2 + 1] = std::tanh(r * 1.05f);
    }
}

// ------------------------------------------------------------------ device

#ifdef __ANDROID__

int32_t AudioEngine::data_callback(AAudioStream*, void* user, void* audio, int32_t frames) {
    auto* self = static_cast<AudioEngine*>(user);
    self->mix(static_cast<float*>(audio), frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

bool AudioEngine::start() {
    voices_.resize(MAX_VOICES);

    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) {
        loge("AAudio builder failed");
        return false;
    }
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, MIX_RATE);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    // Usage and content-type hints are API 28+ and purely advisory. minSdk here
    // is 26, so they are left off entirely rather than dlsym'd for no gain.
    AAudioStreamBuilder_setDataCallback(b, data_callback, this);

    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &stream_);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK || !stream_) {
        loge("AAudio open failed: %d", (int)r);
        return false;
    }
    // The device may hand back a different rate; resampling every clip would be
    // worse than letting playback ride at the device rate, so just log it.
    int actual = AAudioStream_getSampleRate(stream_);
    if (actual != MIX_RATE) logi("AAudio opened at %d Hz (assets are %d Hz)", actual, MIX_RATE);
    AAudioStream_requestStart(stream_);
    running_ = true;
    logi("audio up: %d voices", MAX_VOICES);
    return true;
}

void AudioEngine::stop() {
    if (stream_) {
        AAudioStream_requestStop(stream_);
        AAudioStream_close(stream_);
        stream_ = nullptr;
    }
    running_ = false;
}

void AudioEngine::pause() {
    if (stream_ && running_) AAudioStream_requestPause(stream_);
}

void AudioEngine::resume() {
    if (stream_ && running_) AAudioStream_requestStart(stream_);
}

#else

// Host builds have no device; the mixer still runs so it can be unit-tested.
bool AudioEngine::start() {
    voices_.resize(MAX_VOICES);
    running_ = true;
    return true;
}
void AudioEngine::stop() { running_ = false; }
void AudioEngine::pause() {}
void AudioEngine::resume() {}
int32_t AudioEngine::data_callback(AAudioStream*, void*, void*, int32_t) { return 0; }

#endif

}  // namespace hl

// stb_vorbis brings in its own implementation; keep it out of the header pass.
#define STB_VORBIS_NO_STDIO
#include "../third_party/stb_vorbis.c"
