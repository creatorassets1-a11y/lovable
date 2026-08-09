// Headless smoke test for the real game.
//
// Boots the shipping Game class against a surfaceless GLES3 context, drives it
// with synthetic input for a few simulated minutes, and asserts the things that
// are cheap to check and expensive to discover on a phone: no crash, the player
// never ends up inside geometry, the Matron can actually reach the player, the
// chapter flow advances, and every frame produces a lit image.
//
//   ./matron_game <asset-dir> <out-dir> [--frames N] [--chapter N]

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../android/app/src/main/cpp/audio/audio.h"
#include "../android/app/src/main/cpp/core/asset.h"
#include "../android/app/src/main/cpp/game/game.h"
#include "../android/app/src/main/cpp/game/story.h"

#include "stb_write_shim.h"

using namespace hl;
using namespace hm;

// Small by default: this is a logic smoke test, and llvmpipe costs a
// second a frame at 720p. --size raises it when a screenshot is wanted.
static int WIDTH = 640;
static int HEIGHT = 360;

static int g_failures = 0;
static void check(bool ok, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::printf("%s %s\n", ok ? "  ok  " : " FAIL ", buf);
    if (!ok) g_failures++;
}

static bool init_egl() {
    auto getPD = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = EGL_NO_DISPLAY;
    if (getPD) dpy = getPD(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    if (dpy == EGL_NO_DISPLAY) dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) return false;
    EGLint a, b;
    if (!eglInitialize(dpy, &a, &b)) return false;
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint cfga[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8,
                           EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                           EGL_DEPTH_SIZE, 24, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                           EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(dpy, cfga, &cfg, 1, &n) || n < 1) return false;
    const EGLint ctxa[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxa);
    if (ctx == EGL_NO_CONTEXT) return false;
    return eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
}

int main(int argc, char** argv) {
    std::string asset_dir = argc > 1 ? argv[1] : "android/app/src/main/assets";
    std::string out_dir = argc > 2 ? argv[2] : "dist/shots3d";
    int frames = 2400;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--size") && i + 2 < argc) {
            WIDTH = atoi(argv[++i]);
            HEIGHT = atoi(argv[++i]);
        }
    }

    if (!init_egl()) { std::fprintf(stderr, "EGL init failed\n"); return 1; }
    std::printf("GL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    GLuint fbo = 0, color = 0, depth = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, WIDTH, HEIGHT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, WIDTH, HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "fbo incomplete\n");
        return 1;
    }

    asset_set_directory(asset_dir);

    AudioEngine audio;
    audio.start();      // host build: mixer only, no device

    Game game;
    if (!game.init(WIDTH, HEIGHT, &audio)) {
        std::fprintf(stderr, "game init failed\n");
        return 1;
    }
    game.set_output_framebuffer(fbo);
    game.set_insets(48, 24, 48, 24);
    check(true, "game initialised");

    // Clips referenced by the story must all have decoded.
    int missing_sfx = 0, missing_vo = 0;
    for (int i = 0; i < story::SFX_COUNT; i++) {
        if (!audio.has_clip(story::sfx_name(i))) { missing_sfx++; }
    }
    for (int i = 0; i < story::VOICE_COUNT; i++) {
        if (!audio.has_clip(story::voice_id(i))) { missing_vo++; }
    }
    check(missing_sfx == 0, "all %d sfx clips decoded (%d missing)",
          story::SFX_COUNT, missing_sfx);
    check(missing_vo == 0, "all %d voice lines decoded (%d missing)",
          story::VOICE_COUNT, missing_vo);

    // ---- drive it
    Input in;
    in.any_tap = true;      // leave the title
    const float dt = 1.0f / 60.0f;
    float t = 0;

    int chapters_seen = 1;
    int last_chapter = 1;
    int deaths = 0;
    Phase last_phase = Phase::Title;
    int inside_wall = 0;
    float min_matron_dist = 1e9f;
    float darkest = 1e9f, brightest = 0;
    int lit_frames = 0, sampled = 0;
    Rng rng(7);

    std::vector<uint8_t> px((size_t)WIDTH * HEIGHT * 4);

    for (int f = 0; f < frames; f++) {
        // Synthetic play: wander forward, sweep the view, occasionally sprint,
        // and mash interact so notes get read and exits get taken.
        // Steer toward the current objective using the same flow field the
        // Matron uses, so the bot actually completes chapters instead of
        // bouncing off the first wall it meets.
        v3 goal;
        if (game.objective_position(&goal)) {
            v3 step;
            if (game.navigate_from(game.player().pos, goal, &step)) {
                float want = std::atan2(step.z, step.x);
                float d = want - game.player().yaw;
                while (d > PI) d -= TAU;
                while (d < -PI) d += TAU;
                in.look = v2(clampf(d, -0.06f, 0.06f), 0);
                in.move = v2(0, 1.0f);
            }
        } else {
            in.move = v2(std::sin(t * 0.37f) * 0.5f, 0.9f);
            in.look = v2(std::sin(t * 0.21f) * 0.010f, 0);
        }
        in.run = std::fmod(t, 11.0f) < 2.5f;
        in.crouch = std::fmod(t, 17.0f) < 3.0f;
        in.interact = (f % 12 == 0);
        in.torch = false;
        in.pause = false;
        in.any_tap = (f % 40 == 0);

        game.update(dt, in);
        game.render(t);
        t += dt;

        if (game.chapter() != last_chapter) {
            last_chapter = game.chapter();
            chapters_seen++;
        }
        // Count transitions, not frames spent on the death screen.
        if (game.phase() == Phase::Dead && last_phase != Phase::Dead) deaths++;
        last_phase = game.phase();

        if (f % 30 == 0) {
            const Player& p = game.player();
            if (game.phase() == Phase::Playing && game.solid_at_player()) inside_wall++;
            min_matron_dist = std::min(min_matron_dist, game.matron_distance());

            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            double sum = 0;
            int mx = 0;
            for (size_t i = 0; i < px.size(); i += 4 * 211) {
                int v = (px[i] + px[i + 1] + px[i + 2]) / 3;
                sum += v;
                if (v > mx) mx = v;
            }
            double mean = sum / (px.size() / (4 * 211));
            darkest = std::min(darkest, (float)mean);
            brightest = std::max(brightest, (float)mean);
            if (mx > 40) lit_frames++;
            sampled++;
        }

        if (f == 400 || f == 1400 || f == 2300) {
            char name[256];
            snprintf(name, sizeof(name), "%s/play_%04d.png", out_dir.c_str(), f);
            write_png_flipped(name, WIDTH, HEIGHT, px.data());
        }
    }

    check(inside_wall == 0, "player never inside geometry (%d samples bad)", inside_wall);
    check(lit_frames > sampled / 2, "frames are lit (%d/%d sampled above threshold)",
          lit_frames, sampled);
    check(chapters_seen >= 2, "chapter flow advanced (%d chapters entered)", chapters_seen);
    check(min_matron_dist < 40.0f, "Matron pathed toward the player (closest %.1f m)",
          min_matron_dist);
    check(brightest > 8.0f, "scene brightness range %.1f .. %.1f", darkest, brightest);

    std::printf("\nsimulated %.0f s of play, %d deaths\n", t, deaths);
    game.shutdown();
    audio.stop();

    std::printf(g_failures ? "\n%d CHECK(S) FAILED\n" : "\nALL CHECKS PASSED\n", g_failures);
    return g_failures ? 1 : 0;
}
