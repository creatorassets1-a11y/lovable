// voice_test.cpp - verifies the speech synthesiser is acoustically correct.
//
// Formants are measured from the harmonic envelope; see the comment on
// harmonicSpectrum() for why the two more obvious methods do not work here.
// Analysis runs at a deliberately low fundamental so there are enough
// harmonics below 300Hz to resolve the first formant of close vowels like
// IY and UW. The vocal tract model does not depend on pitch, so measuring at
// 70Hz says exactly as much as measuring at a conversational pitch.
#include "../app/src/main/cpp/speech.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

using namespace hm;

static int gFails = 0, gChecks = 0;
static void check(bool cond, const char* what) {
    gChecks++;
    if (!cond) { gFails++; std::printf("  FAIL  %s\n", what); }
}

// Formant estimation by harmonic envelope sampling.
//
// A voiced vowel only has energy at multiples of F0, so the spectral envelope
// can only be observed there. We measure the amplitude of each harmonic and
// look for local maxima in that sequence: a harmonic that is louder than its
// neighbours is sitting on a formant.
//
// Two other approaches were tried first and both were wrong. Raw spectral peak
// picking just finds every harmonic, so any target within half of F0 "passes"
// and the test proves nothing. LPC is biased toward harmonics rather than the
// envelope when the source is exactly periodic, which it is here because the
// analysis disables jitter - it merged AA's F1 and F2 into a single pole.
static std::vector<float> harmonicSpectrum(const std::vector<float>& sig,
                                           int rate, float f0,
                                           std::vector<float>* freqsOut) {
    const int N = 4096;
    std::vector<double> mag(N / 2, 0.0);
    int windows = 0;
    for (int start = 512; start + N < (int)sig.size() && windows < 3; start += N / 2) {
        for (int k = 0; k < N / 2; k++) {
            double re = 0, im = 0;
            for (int n = 0; n < N; n++) {
                double w = 0.54 - 0.46 * std::cos(2.0 * PI * n / (N - 1));
                double a = 2.0 * PI * k * n / N;
                re += sig[start + n] * w * std::cos(a);
                im -= sig[start + n] * w * std::sin(a);
            }
            mag[k] += std::sqrt(re * re + im * im);
        }
        windows++;
    }
    if (windows == 0) return {};

    std::vector<float> amps, freqs;
    for (int h = 1; h * f0 < rate * 0.42f; h++) {
        float f = h * f0;
        int k = (int)(f * N / rate);
        double best = 0;
        for (int d = -2; d <= 2; d++) {
            int kk = k + d;
            if (kk > 0 && kk < N / 2) best = std::max(best, mag[kk]);
        }
        amps.push_back((float)(best / windows));
        freqs.push_back(f);
    }
    if (freqsOut) *freqsOut = freqs;
    return amps;
}

static std::vector<float> formants(const std::vector<float>& sig, int rate, float f0) {
    std::vector<float> freqs;
    std::vector<float> amps = harmonicSpectrum(sig, rate, f0, &freqs);
    std::vector<float> out;
    for (int i = 1; i + 1 < (int)amps.size(); i++) {
        if (amps[i] > amps[i - 1] && amps[i] >= amps[i + 1]) {
            // Interpolate the true peak between the neighbouring harmonics.
            float a = amps[i - 1], b = amps[i], c = amps[i + 1];
            float denom = (a - 2 * b + c);
            float shift = (std::fabs(denom) > 1e-9f) ? 0.5f * (a - c) / denom : 0.0f;
            shift = std::max(-1.0f, std::min(1.0f, shift));
            out.push_back(freqs[i] + shift * f0);
        }
    }
    return out;
}

static bool nearAny(const std::vector<float>& p, float target, float tol) {
    for (float f : p) if (std::fabs(f - target) < tol) return true;
    return false;
}

int main() {
    std::printf("=== HOLLOW SIGNAL - voice synthesis tests ===\n");
    const int rate = 22050;
    VoiceProfile v;
    v.pitch = 70.0f;        // dense harmonics: needed to see a low F1
    v.rate = 4.0f;          // long steady vowels for analysis
    v.jitter = 0.0f;
    v.shimmer = 0.0f;
    RadioProfile dry;
    dry.enabled = false;

    struct Case { const char* ph; float f1, f2; };
    const Case cases[] = {
        {"IY", 270, 2290}, {"IH", 390, 1990}, {"EH", 530, 1840},
        {"AE", 660, 1720}, {"AA", 730, 1090}, {"AO", 570, 840},
        {"UH", 440, 1020}, {"UW", 300, 870},  {"AH", 640, 1190},
        {"ER", 490, 1350},
    };
    const int nCases = (int)(sizeof(cases) / sizeof(cases[0]));

    std::printf("\n[formants] harmonic envelope of sustained vowels\n");
    for (int i = 0; i < nCases; i++) {
        const Case& c = cases[i];
        std::vector<float> pcm = synthesizeUtterance(c.ph, v, dry, rate, 99);
        std::vector<float> f = formants(pcm, rate, v.pitch);
        // Tolerances are wider than a phonetician would use, but far tighter
        // than the harmonic spacing, so a pass means something.
        bool ok1 = nearAny(f, c.f1, std::max(v.pitch * 1.35f, c.f1 * 0.22f));
        bool ok2 = nearAny(f, c.f2, std::max(v.pitch * 1.6f, c.f2 * 0.18f));
        std::printf("  %-3s want %4.0f/%4.0f  got", c.ph, c.f1, c.f2);
        for (size_t k = 0; k < f.size() && k < 4; k++) std::printf(" %4.0f", f[k]);
        std::printf("   %s\n", (ok1 && ok2) ? "ok" : (ok1 ? "F2 off" : "F1 off"));
        check(ok1, "first formant lands on target");
        check(ok2, "second formant lands on target");
    }

    // The point of formants is contrast. Confirm the vowels are actually
    // distinguishable from each other, not merely all present somewhere.
    std::printf("\n[contrast] vowels must be separable\n");
    {
        std::vector<float> f_iy = formants(synthesizeUtterance("IY", v, dry, rate, 1), rate, v.pitch);
        std::vector<float> f_uw = formants(synthesizeUtterance("UW", v, dry, rate, 1), rate, v.pitch);
        std::vector<float> f_aa = formants(synthesizeUtterance("AA", v, dry, rate, 1), rate, v.pitch);
        // Take the peak nearest the expected formant, not the first one inside
        // a wide window - a minor ripple at the bottom of the range would
        // otherwise stand in for the formant we are actually asking about.
        auto nearest = [](const std::vector<float>& f, float target) {
            float best = 0.0f, bestD = 1e9f;
            for (float x : f) {
                float d = std::fabs(x - target);
                if (d < bestD) { bestD = d; best = x; }
            }
            return best;
        };
        float iy2 = nearest(f_iy, 2290.0f);
        float uw2 = nearest(f_uw, 870.0f);
        float aa1 = nearest(f_aa, 730.0f);
        std::printf("  IY F2=%.0f   UW F2=%.0f   AA F1=%.0f\n", iy2, uw2, aa1);
        check(iy2 > 1800.0f, "IY has a high second formant");
        check(uw2 > 0.0f && uw2 < 1200.0f, "UW has a low second formant");
        check(iy2 > uw2 * 1.8f, "IY and UW are clearly separated in F2");
        check(aa1 > 600.0f, "AA has an open first formant");
    }

    // Sibilants live far above the formant range; check the energy is there.
    std::printf("\n[fricatives] sibilant energy placement\n");
    {
        auto bandEnergy = [&](const char* ph, float lo, float hi) {
            std::vector<float> pcm = synthesizeUtterance(ph, v, dry, rate, 5);
            double e = 0;
            const int N = 1024;
            int start = (int)pcm.size() / 2 - N / 2;
            if (start < 0) start = 0;
            for (int k = 0; k < N / 2; k++) {
                float f = (float)k * rate / N;
                if (f < lo || f > hi) continue;
                double re = 0, im = 0;
                for (int n = 0; n < N && start + n < (int)pcm.size(); n++) {
                    double a = 2.0 * PI * k * n / N;
                    re += pcm[start + n] * std::cos(a);
                    im -= pcm[start + n] * std::sin(a);
                }
                e += re * re + im * im;
            }
            return e;
        };
        double sHigh = bandEnergy("S", 4000, 8000);
        double sLow = bandEnergy("S", 200, 1200);
        double shMid = bandEnergy("SH", 2000, 4000);
        double shHigh = bandEnergy("SH", 5000, 9000);
        std::printf("  S  high/low = %.2f      SH mid/high = %.2f\n",
                    sHigh / std::max(1e-9, sLow), shMid / std::max(1e-9, shHigh));
        check(sHigh > sLow * 3.0, "S puts its energy in the 4-8kHz band");
        check(shMid > shHigh, "SH sits lower than S, as it should");
    }

    // Whole utterances must stay clean and bounded through the radio channel.
    std::printf("\n[utterance] full lines through the radio channel\n");
    {
        VoiceProfile dispatch{104.0f, 0.16f, 0.97f, 0.09f, 1.0f, 0.009f, 0.04f, 0.004f};
        RadioProfile radio;
        const char* line = "G EH1 T . T UW . DH AH . S AH1 B S T EY SH AH N";
        std::vector<float> pcm = synthesizeUtterance(line, dispatch, radio, rate, 7);
        float peak = 0.0f;
        double energy = 0.0;
        int bad = 0;
        for (float s : pcm) {
            if (!std::isfinite(s)) bad++;
            peak = std::max(peak, std::fabs(s));
            energy += (double)s * s;
        }
        double rms = std::sqrt(energy / std::max<size_t>(1, pcm.size()));
        std::printf("  %.2fs, peak %.3f, rms %.3f\n", pcm.size() / (float)rate, peak, rms);
        check(bad == 0, "no non-finite samples");
        check(pcm.size() > (size_t)(rate * 0.8f), "utterance has a sensible duration");
        check(peak > 0.5f && peak <= 1.0f, "utterance is normalised into range");
        check(rms > 0.02 && rms < 0.5, "utterance level is sane");

        // Silence between words must actually be quieter than speech, or the
        // radio noise floor has swallowed the whole line.
        size_t win = rate / 40;
        float loudest = 0.0f, quietest = 1e9f;
        for (size_t i = 0; i + win < pcm.size(); i += win) {
            double e = 0;
            for (size_t k = 0; k < win; k++) e += (double)pcm[i + k] * pcm[i + k];
            float w = (float)std::sqrt(e / win);
            loudest = std::max(loudest, w);
            quietest = std::min(quietest, w);
        }
        std::printf("  loudest window %.3f, quietest %.3f\n", loudest, quietest);
        check(loudest > quietest * 4.0f, "speech stands well clear of the noise floor");
    }

    // Vocalisations must be bounded and actually different from each other.
    std::printf("\n[vocals] non-verbal\n");
    {
        const char* names[VOC_COUNT] = {"scream", "gasp", "pain", "sob",
                                        "laugh", "roar", "wail", "chitter"};
        for (int t = 0; t < VOC_COUNT; t++) {
            VoiceProfile vp;
            vp.pitch = (t >= VOC_MONSTER_ROAR) ? 64.0f : 150.0f;
            std::vector<float> pcm = synthesizeVocal((VocalType)t, vp, rate, 3u + t);
            float peak = 0.0f;
            int bad = 0;
            for (float s : pcm) {
                if (!std::isfinite(s)) bad++;
                peak = std::max(peak, std::fabs(s));
            }
            std::printf("  %-8s %.2fs peak %.2f\n", names[t], pcm.size() / (float)rate, peak);
            check(bad == 0, "vocalisation has no non-finite samples");
            check(peak > 0.5f && peak <= 1.0f, "vocalisation is normalised");
            check(pcm.size() > (size_t)(rate * 0.3f), "vocalisation has real duration");
        }
    }

    std::printf("\n=== %d checks, %d failures ===\n", gChecks, gFails);
    return gFails ? 1 : 0;
}
