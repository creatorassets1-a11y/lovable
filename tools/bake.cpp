// bake.cpp - the offline asset build.
//
// Generates every texture and every recorded-style sound in the game and packs
// them into app/src/main/assets/hollow.pak. Run by tools/bake.sh before the
// Gradle build. The output is deterministic: same source, same bytes.
#include "materials.h"
#include "../app/src/main/cpp/pack.h"
#include "../app/src/main/cpp/speech.h"
#include "../app/src/main/cpp/noise.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>

using namespace hm;

// ---------------------------------------------------------------- pack writer

struct PackWriter {
    struct Blob {
        PakEntry entry;
        std::vector<uint8_t> data;
    };
    std::vector<Blob> blobs;

    void add(const char* name, uint32_t type, uint32_t w, uint32_t h,
             const void* data, size_t bytes) {
        Blob b;
        std::memset(&b.entry, 0, sizeof(PakEntry));
        std::snprintf(b.entry.name, PAK_NAME_LEN, "%s", name);
        b.entry.type = type;
        b.entry.w = w;
        b.entry.h = h;
        b.entry.mips = 0;
        b.entry.size = bytes;
        b.data.resize(bytes);
        std::memcpy(b.data.data(), data, bytes);
        blobs.push_back(std::move(b));
    }

    bool write(const char* path) {
        PakHeader hdr{PAK_MAGIC, PAK_VERSION, (uint32_t)blobs.size(), 0};
        uint64_t offset = pakAlign(sizeof(PakHeader) + sizeof(PakEntry) * blobs.size());
        for (Blob& b : blobs) {
            b.entry.offset = offset;
            offset = pakAlign(offset + b.entry.size);
        }
        FILE* f = std::fopen(path, "wb");
        if (!f) { std::printf("cannot open %s for writing\n", path); return false; }
        std::fwrite(&hdr, sizeof(hdr), 1, f);
        for (const Blob& b : blobs) std::fwrite(&b.entry, sizeof(PakEntry), 1, f);

        const uint8_t pad[16] = {0};
        auto padTo = [&](uint64_t target) {
            uint64_t cur = (uint64_t)std::ftell(f);
            while (cur < target) {
                size_t n = (size_t)std::min<uint64_t>(16, target - cur);
                std::fwrite(pad, 1, n, f);
                cur += n;
            }
        };
        for (const Blob& b : blobs) {
            padTo(b.entry.offset);
            std::fwrite(b.data.data(), 1, b.data.size(), f);
        }
        uint64_t total = (uint64_t)std::ftell(f);
        std::fclose(f);
        std::printf("\nwrote %s: %d entries, %.1f MB\n", path,
                    (int)blobs.size(), total / (1024.0 * 1024.0));
        return true;
    }
};

// ------------------------------------------------------------------ textures

static const int TEX_SIZE = 512;

static void bakeTextures(PackWriter& pack) {
    std::printf("textures (%dx%d albedo + normal/roughness)\n", TEX_SIZE, TEX_SIZE);
    int n = bake::materialCount();
    for (int i = 0; i < n; i++) {
        const bake::MaterialDef& def = bake::materialDef(i);
        bake::Material m;
        m.alloc(TEX_SIZE);
        def.generate(m);
        std::vector<uint8_t> nrm = m.buildNormalRoughness(def.bump);

        std::string an = std::string(def.name) + "_a";
        std::string nn = std::string(def.name) + "_n";
        pack.add(an.c_str(), PAK_TEX_ALBEDO, TEX_SIZE, TEX_SIZE,
                 m.albedo.data(), m.albedo.size());
        pack.add(nn.c_str(), PAK_TEX_NORMAL, TEX_SIZE, TEX_SIZE,
                 nrm.data(), nrm.size());
        std::printf("  %-16s albedo + normal\n", def.name);
    }
}

// --------------------------------------------------------------------- voice

static const int VOICE_RATE = 22050;

struct Line {
    const char* name;
    const char* phonemes;
    int voice;      // index into kVoices
    bool radio;
};

static const VoiceProfile kVoices[] = {
    // dispatch: older man, measured, slightly tired
    {104.0f, 0.16f, 0.97f, 0.09f, 1.00f, 0.009f, 0.040f, 0.004f},
    // survivor (higher register): frightened, faster, unsteady
    {196.0f, 0.26f, 1.16f, 0.16f, 1.12f, 0.020f, 0.075f, 0.030f},
    // survivor (lower register): exhausted, slow
    {112.0f, 0.14f, 1.00f, 0.14f, 0.92f, 0.014f, 0.055f, 0.018f},
    // engineer: clipped, professional
    {126.0f, 0.20f, 1.04f, 0.07f, 1.06f, 0.008f, 0.035f, 0.000f},
};

// ARPABET transcriptions. Digits mark stress: it lengthens the vowel and lifts
// the pitch contour, which is most of what stops synthetic speech sounding flat.
static const Line kLines[] = {
{"vo_dispatch_open",  "D IH1 S P AE CH . T UW . EH1 N IY . Y UW1 N IH T . AA N . DH IH S . CH AE1 N AH L", 0, true},
{"vo_m1_brief",       "G EH1 T . T UW . DH AH . S AH1 B S T EY SH AH N | R IY S T AO1 R . DH AH . P AW1 ER", 0, true},
{"vo_m2_brief",       "DH EH1 R . AA R . S ER V AY1 V ER Z . IH N . DH AH . K L IH1 N IH K | F AY1 N D . DH EH M", 0, true},
{"vo_m3_brief",       "DH AH . B R IH1 JH . IH Z . D AW1 N | Y UW . HH AE V . T UW . G OW . TH R UW . DH AH . T AH1 N AH L Z", 0, true},
{"vo_m4_brief",       "F AY1 N D . DH AH . R IY L EY1 . AA N . DH AH . R UW1 F . AH V . DH AH . T AW1 ER", 0, true},
{"vo_m5_brief",       "G EH1 T . T UW . DH AH . HH AA1 R B ER . B IY1 F AO R . S AH1 N R AY Z", 0, true},
{"vo_warn_close",     "IH T . IH Z . K L OW1 S . T UW . Y UW | D UW . N AA1 T . R AH1 N", 0, true},
{"vo_warn_dark",      "DH AH . L AY1 T S . AA R . G AO1 N . AA N . Y AO R . B L AA1 K", 0, true},
{"vo_lost_signal",    "AY . AE M . L UW1 Z IH NG . Y AO1 R . S IH1 G N AH L", 0, true},
{"vo_good",           "G UH1 D | K IY1 P . M UW1 V IH NG", 0, true},
{"vo_hurry",          "HH ER1 IY | IH T . IH Z . K AH1 M IH NG", 0, true},
{"vo_last_one",       "DH AE1 T . IH Z . DH AH . L AE1 S T . W AH N | G EH1 T . AW1 T", 0, true},
{"vo_objective_done", "AA1 R IY AH . K L IH1 R | M UW1 V . T UW . DH AH . N EH1 K S T", 0, true},
{"vo_dispatch_dead",  "IH F . Y UW . K AE N . HH IY1 R . M IY | AY . AE M . S AA1 R IY", 0, true},

{"vo_sv_help",        "HH EH1 L P . M IY | P L IY1 Z", 1, false},
{"vo_sv_quiet",       "B IY . K W AY1 AH T | IH T . HH IY1 R Z . Y UW", 1, false},
{"vo_sv_follow",      "OW K EY1 | AY . AE M . W IH DH . Y UW1", 1, false},
{"vo_sv_wait",        "W EY1 T | D UW . N AA1 T . L IY1 V . M IY", 1, false},
{"vo_sv_down",        "G EH1 T . D AW1 N | N AW1", 1, false},
{"vo_sv_thanks",      "TH AE1 NG K . Y UW | TH AE1 NG K . Y UW", 1, false},
{"vo_sv_cant",        "AY . K AE1 N . N AA T . D UW . DH IH1 S", 1, false},
{"vo_sv_there",       "IH T . IH Z . OW1 V ER . DH EH1 R", 1, false},
{"vo_sv_run",         "R AH1 N | R AH1 N", 1, false},

{"vo_sm_alldead",     "DH EY . AA R . AO1 L . D EH1 D", 2, false},
{"vo_sm_notgoing",    "AY . AE M . N AA1 T . G OW IH NG . B AE1 K . DH EH R", 2, false},
{"vo_sm_water",       "DH AH . W AO1 T ER . IH Z . R AY1 Z IH NG", 2, false},
{"vo_sm_saw",         "AY . S AO1 . IH T | IH T . W AA1 Z . N AA T . AH . M AE1 N", 2, false},
{"vo_sm_leave",       "JH AH1 S T . L IY1 V . M IY . HH IY R", 2, false},

{"vo_en_power",       "P AW1 ER . IH Z . B AE1 K | DH AH . D AO1 R Z . SH UH D . OW1 P AH N", 3, true},
{"vo_en_generator",   "DH AH . JH EH1 N ER EY T ER . N IY1 D Z . TH R IY . S EH1 L Z", 3, true},
{"vo_en_roof",        "DH AH . S T EH1 R W EH L . G OW1 Z . AO1 L . DH AH . W EY . AH P", 3, true},
{"vo_en_careful",     "B IY . K EH1 R F AH L . AH P . DH EH1 R", 3, true},
};

static void bakeVoice(PackWriter& pack) {
    std::printf("\nvoice lines (formant synthesis, %d Hz)\n", VOICE_RATE);
    int n = (int)(sizeof(kLines) / sizeof(kLines[0]));
    uint32_t seed = 0x5150u;
    for (int i = 0; i < n; i++) {
        const Line& L = kLines[i];
        RadioProfile radio;
        radio.enabled = L.radio;
        if (!L.radio) {
            // Voices in the room still get band-limited a little; a completely
            // full-band synthetic voice sounds more artificial, not less.
            radio.enabled = true;
            radio.noise = 0.004f;
            radio.dropout = 0.0f;
            radio.drive = 1.25f;
            radio.squelch = 0.0f;
            radio.lowCut = 110.0f;
            radio.highCut = 7200.0f;
        }
        std::vector<float> pcm = synthesizeUtterance(L.phonemes, kVoices[L.voice],
                                                     radio, VOICE_RATE, seed + i * 977u);
        std::vector<int16_t> out(pcm.size());
        for (size_t k = 0; k < pcm.size(); k++)
            out[k] = (int16_t)(clampf(pcm[k], -1.0f, 1.0f) * 32000.0f);
        pack.add(L.name, PAK_AUDIO_MONO, VOICE_RATE, (uint32_t)out.size(),
                 out.data(), out.size() * sizeof(int16_t));
        std::printf("  %-22s %5.2fs\n", L.name, pcm.size() / (float)VOICE_RATE);
    }

    // Non-verbal vocalisations, several takes each so they do not repeat
    // audibly. Repetition is what kills a scream.
    static const char* kVocNames[VOC_COUNT] = {
        "vx_scream", "vx_gasp", "vx_pain", "vx_sob",
        "vx_laugh", "vx_roar", "vx_wail", "vx_chitter"
    };
    std::printf("\nvocalisations\n");
    for (int t = 0; t < VOC_COUNT; t++) {
        for (int take = 0; take < 3; take++) {
            VoiceProfile vp = kVoices[(t < 5) ? (take % 3) : 2];
            if (t >= VOC_MONSTER_ROAR) {
                vp.pitch = 62.0f + take * 9.0f;
                vp.formantScale = 0.74f + take * 0.05f;
                vp.jitter = 0.06f;
                vp.shimmer = 0.14f;
            }
            std::vector<float> pcm = synthesizeVocal((VocalType)t, vp, VOICE_RATE,
                                                     0x9001u + t * 131u + take * 17u);
            std::vector<int16_t> out(pcm.size());
            for (size_t k = 0; k < pcm.size(); k++)
                out[k] = (int16_t)(clampf(pcm[k], -1.0f, 1.0f) * 32000.0f);
            char name[PAK_NAME_LEN];
            std::snprintf(name, sizeof(name), "%s_%d", kVocNames[t], take);
            pack.add(name, PAK_AUDIO_MONO, VOICE_RATE, (uint32_t)out.size(),
                     out.data(), out.size() * sizeof(int16_t));
        }
        std::printf("  %-22s 3 takes\n", kVocNames[t]);
    }
}

// ------------------------------------------------------------------ ambience

// Long stereo beds. Baking these offline rather than synthesising them live
// means they can be minutes long and never audibly loop, which short loops
// always eventually do.
struct Bed {
    const char* name;
    float seconds;
    int kind;   // 0 street, 1 interior, 2 tunnels
};

static const Bed kBeds[] = {
    {"amb_street",   50.0f, 0},
    {"amb_interior", 50.0f, 1},
    {"amb_tunnels",  50.0f, 2},
};

static void bakeAmbience(PackWriter& pack) {
    std::printf("\nambience beds\n");
    const int rate = 22050;
    for (const Bed& bed : kBeds) {
        int n = (int)(bed.seconds * rate);
        std::vector<int16_t> out((size_t)n * 2);

        uint32_t rng = 0x1234u + bed.kind * 7717u;
        auto nz = [&]() {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return ((int32_t)(rng >> 8) * (1.0f / 8388608.0f)) - 1.0f;
        };

        float lp1 = 0, lp2 = 0, lp3 = 0, hp = 0, prev = 0;
        float dronePh = 0, dronePh2 = 0;
        float gustPh = 0;
        float eventTimer = 0.0f;
        float eventEnv = 0.0f, eventPh = 0.0f, eventPan = 0.5f;
        int eventType = 0;

        for (int i = 0; i < n; i++) {
            float t = (float)i / rate;
            float wn = nz();

            // Broadband bed, filtered differently per location.
            float cut = (bed.kind == 0) ? 0.010f : (bed.kind == 1 ? 0.004f : 0.0022f);
            lp1 += (wn - lp1) * cut;
            lp2 += (lp1 - lp2) * cut;
            float rumble = lp2 * ((bed.kind == 2) ? 9.0f : 5.5f);

            // Wind, only outdoors: gusts modulate a band of noise.
            float wind = 0.0f;
            if (bed.kind == 0) {
                gustPh += 0.045f / rate * 60.0f;
                float gust = 0.45f + 0.55f * std::sin(gustPh * TAU * 0.11f)
                                   * std::sin(gustPh * TAU * 0.037f + 1.3f);
                lp3 += (wn - lp3) * (0.02f + 0.03f * gust);
                float band = lp3 - lp2;
                wind = band * gust * 2.6f;
            }

            // Room tone: a low hum with a beating partial. Every interior has one.
            dronePh += ((bed.kind == 1) ? 50.0f : 34.0f) / rate;
            dronePh2 += ((bed.kind == 1) ? 50.35f : 34.4f) / rate;
            float drone = (std::sin(dronePh * TAU) + std::sin(dronePh2 * TAU) * 0.7f)
                          * ((bed.kind == 1) ? 0.028f : 0.016f);

            // Sparse foreground events, panned, so the bed has a foreground.
            eventTimer -= 1.0f / rate;
            if (eventTimer <= 0.0f) {
                eventTimer = 1.2f + (nz() * 0.5f + 0.5f) * ((bed.kind == 2) ? 3.0f : 5.5f);
                eventEnv = 1.0f;
                eventPh = 0.0f;
                eventPan = nz() * 0.5f + 0.5f;
                eventType = (int)((nz() * 0.5f + 0.5f) * 4.0f) & 3;
            }
            float ev = 0.0f;
            if (eventEnv > 0.0005f) {
                eventPh += 1.0f / rate;
                switch (bed.kind == 2 ? (eventType & 1) : eventType) {
                case 0: {   // drip
                    float f = 1400.0f * std::exp(-eventPh * 10.0f) + 190.0f;
                    ev = std::sin(eventPh * f * TAU) * std::exp(-eventPh * 9.0f) * 0.30f;
                    break;
                }
                case 1: {   // distant metal
                    float f = 320.0f;
                    ev = (std::sin(eventPh * f * TAU) * 0.5f
                        + std::sin(eventPh * f * 2.71f * TAU) * 0.3f)
                        * std::exp(-eventPh * 2.4f) * 0.22f;
                    break;
                }
                case 2: {   // creak
                    float wob = 0.05f + 0.03f * std::sin(eventPh * 7.0f * TAU);
                    hp += (wn - hp) * wob;
                    ev = (hp - lp1) * 2.0f * std::exp(-eventPh * 1.6f) * 0.5f;
                    break;
                }
                default: {  // distant collapse
                    lp1 += (wn - lp1) * 0.02f;
                    ev = lp1 * std::exp(-eventPh * 1.1f) * 1.4f;
                    break;
                }
                }
                eventEnv *= 0.99994f;
                if (eventPh > 3.5f) eventEnv = 0.0f;
            }

            float mono = rumble * 0.22f + wind * 0.16f + drone;
            float l = mono + ev * (1.0f - eventPan);
            float r = mono + ev * eventPan;

            // Gentle decorrelation between channels so it feels wide.
            prev = lerpf(prev, wn, 0.02f);
            l += prev * 0.006f;
            r -= prev * 0.006f;

            out[(size_t)i * 2 + 0] = (int16_t)(clampf(l, -1.0f, 1.0f) * 26000.0f);
            out[(size_t)i * 2 + 1] = (int16_t)(clampf(r, -1.0f, 1.0f) * 26000.0f);
            (void)t;
        }

        pack.add(bed.name, PAK_AUDIO_STEREO, rate, (uint32_t)n,
                 out.data(), out.size() * sizeof(int16_t));
        std::printf("  %-16s %.0fs stereo\n", bed.name, bed.seconds);
    }
}

// ---------------------------------------------------------------------- main

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "app/src/main/assets/hollow.pak";
    std::printf("=== HOLLOW SIGNAL asset bake ===\n\n");
    PackWriter pack;
    bakeTextures(pack);
    bakeVoice(pack);
    bakeAmbience(pack);
    return pack.write(out) ? 0 : 1;
}
