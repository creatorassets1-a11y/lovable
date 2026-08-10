// render_test.cpp - renders actual frames of the actual game and looks at the
// pixels.
//
// This exists because of a shipped bug that no other test could have caught.
// The material texture arrays were allocated with one mip level but filtered
// LINEAR_MIPMAP_LINEAR, which leaves the texture mipmap-incomplete; GLES
// samples an incomplete texture as opaque black, so every surface in the world
// rendered black while the UI carried on drawing normally. Nothing about the
// simulation was wrong, so the simulation tests all passed.
//
// It runs against desktop Mesa (llvmpipe) through an EGL pbuffer. That is not
// a phone GPU and it cannot tell us anything about performance, but it runs
// the real shaders, the real texture uploads and the real draw calls, and it
// can answer the one question that matters here: is there a picture?
#include "../app/src/main/cpp/gfx.h"
#include "../app/src/main/cpp/world.h"
#include "../app/src/main/cpp/actors.h"
#include "../app/src/main/cpp/assets.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
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

static const int FBW = 480, FBH = 270;

struct Egl {
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLSurface surf = EGL_NO_SURFACE;
    EGLContext ctx = EGL_NO_CONTEXT;

    bool init() {
        EGLint major = 0, minor = 0;

        // There is no window system here, so the default display has nothing
        // to bind to. Mesa's surfaceless platform gives a usable software
        // context; fall back to the default display for machines that do have
        // a display server.
        auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
            eglGetProcAddress("eglGetPlatformDisplayEXT");
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
        if (getPlatformDisplay) {
            dpy = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
            if (dpy != EGL_NO_DISPLAY && !eglInitialize(dpy, &major, &minor))
                dpy = EGL_NO_DISPLAY;
        }
        if (dpy == EGL_NO_DISPLAY) {
            dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
            if (dpy == EGL_NO_DISPLAY) { std::printf("  no EGL display\n"); return false; }
            if (!eglInitialize(dpy, &major, &minor)) {
                std::printf("  eglInitialize failed\n");
                return false;
            }
        }
        if (!eglBindAPI(EGL_OPENGL_ES_API)) return false;

        const EGLint cfgAttribs[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 16,
            EGL_NONE
        };
        EGLConfig cfg;
        EGLint n = 0;
        if (!eglChooseConfig(dpy, cfgAttribs, &cfg, 1, &n) || n < 1) {
            std::printf("  no ES3 pbuffer config\n");
            return false;
        }
        const EGLint pbAttribs[] = { EGL_WIDTH, FBW, EGL_HEIGHT, FBH, EGL_NONE };
        surf = eglCreatePbufferSurface(dpy, cfg, pbAttribs);
        if (surf == EGL_NO_SURFACE) { std::printf("  no pbuffer\n"); return false; }

        const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttribs);
        if (ctx == EGL_NO_CONTEXT) { std::printf("  no ES3 context\n"); return false; }
        if (!eglMakeCurrent(dpy, surf, surf, ctx)) { std::printf("  makeCurrent failed\n"); return false; }
        std::printf("  EGL %d.%d, GL_VERSION = %s\n", major, minor,
                    (const char*)glGetString(GL_VERSION));
        return true;
    }
    ~Egl() {
        if (dpy != EGL_NO_DISPLAY) {
            eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (ctx != EGL_NO_CONTEXT) eglDestroyContext(dpy, ctx);
            if (surf != EGL_NO_SURFACE) eglDestroySurface(dpy, surf);
            eglTerminate(dpy);
        }
    }
};

struct FrameStats {
    float meanLuma = 0.0f;
    float maxLuma = 0.0f;
    float litFraction = 0.0f;   // fraction of pixels above a visible threshold
    int distinctLevels = 0;     // rough measure of how much detail is present
};

static FrameStats readFrame() {
    std::vector<uint8_t> px((size_t)FBW * FBH * 4);
    glReadPixels(0, 0, FBW, FBH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

    FrameStats s;
    double sum = 0;
    int lit = 0;
    int hist[64] = {0};
    for (int i = 0; i < FBW * FBH; i++) {
        float r = px[i * 4 + 0] / 255.0f;
        float g = px[i * 4 + 1] / 255.0f;
        float b = px[i * 4 + 2] / 255.0f;
        float l = 0.299f * r + 0.587f * g + 0.114f * b;
        sum += l;
        s.maxLuma = std::max(s.maxLuma, l);
        if (l > 0.06f) lit++;
        hist[std::min(63, (int)(l * 64.0f))]++;
    }
    s.meanLuma = (float)(sum / (FBW * FBH));
    s.litFraction = lit / (float)(FBW * FBH);
    for (int i = 0; i < 64; i++) if (hist[i] > FBW * FBH / 2000) s.distinctLevels++;
    return s;
}

// Sets up the same scene parameters the game uses, so the test exercises the
// real lighting path rather than a simplified one.
static SceneParams gameLikeScene(const World& w, const vec3& eye, float yaw,
                                 bool torchOn, bool indoors) {
    SceneParams sp;
    vec3 fwd(std::sin(yaw), -0.05f, std::cos(yaw));
    sp.camPos = eye;
    sp.view = mat4::lookAt(eye, eye + fwd, vec3(0, 1, 0));
    sp.proj = mat4::perspective(72.0f * DEG2RAD, (float)FBW / FBH, 0.06f, 220.0f);
    sp.torchPos = eye;
    sp.torchDir = normalize(fwd);
    sp.torchIntensity = torchOn ? 2.15f : 0.0f;
    sp.torchRange = 22.0f;
    sp.torchColor = vec3(1.0f, 0.93f, 0.80f);
    sp.ambient = indoors ? vec3(0.030f, 0.031f, 0.038f) : vec3(0.070f, 0.076f, 0.098f);
    sp.moonColor = indoors ? vec3(0.020f, 0.023f, 0.034f) : vec3(0.105f, 0.118f, 0.165f);
    sp.moonDir = normalize(vec3(0.38f, -0.80f, 0.46f));
    sp.fogColor = indoors ? vec3(0.014f, 0.014f, 0.016f) : vec3(0.020f, 0.022f, 0.030f);
    sp.fogDensity = indoors ? 0.085f : 0.030f;
    sp.numLights = 0;
    (void)w;
    return sp;
}

int main(int argc, char** argv) {
    const char* pakPath = (argc > 1) ? argv[1] : "app/src/main/assets/hollow.pak";
    std::printf("=== HOLLOW SIGNAL - headless render tests ===\n\n[egl]\n");

    Egl egl;
    if (!egl.init()) {
        std::printf("\n  no GL available in this environment - skipping render tests\n");
        return 0;
    }

    AssetPack pack;
    bool havePack = pack.openFile(pakPath);
    std::printf("  asset pack: %s\n", havePack ? "loaded" : "MISSING");

    std::printf("\n[renderer] init\n");
    Renderer r;
    bool ok = r.init(havePack ? &pack : nullptr);
    check(ok, "the renderer initialises");
    if (!ok) { std::printf("\n=== %d checks, %d failures ===\n", gChecks, gFails); return 1; }
    r.resize(FBW, FBH);
    r.setRenderScale(1.0f);

    std::printf("\n[world] build and upload\n");
    World w;
    w.generate(0x4A17E5u, 160, 160);
    std::vector<Chunk> chunks;
    w.buildChunks(chunks, 24);
    std::vector<GpuMesh> meshes(chunks.size());
    for (size_t i = 0; i < chunks.size(); i++) meshes[i].upload(chunks[i].mesh);
    std::printf("  %d chunks uploaded\n", (int)chunks.size());

    GpuMesh actor[BP_COUNT];
    {
        Mesh parts[BP_COUNT];
        buildActorParts(AK_STALKER, parts);
        for (int i = 0; i < BP_COUNT; i++) actor[i].upload(parts[i]);
    }
    check(glGetError() == GL_NO_ERROR, "no GL error after uploading the world");

    // Stand at the spawn looking down the street, torch on: what the player
    // sees in the first second of the game.
    vec3 eye = w.playerStart + vec3(0, 1.66f, 0);
    auto renderOnce = [&](const SceneParams& sp) {
        r.beginShadowPass(sp);
        if (sp.torchIntensity > 0.01f)
            for (size_t i = 0; i < meshes.size(); i++) {
                vec3 c = (chunks[i].mn + chunks[i].mx) * 0.5f;
                if (length(vec3(c.x - sp.camPos.x, 0, c.z - sp.camPos.z)) > 40.0f) continue;
                r.drawShadow(meshes[i], mat4());
            }
        r.endShadowPass();
        r.beginScene(sp);
        {
            GLint fb = -1;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
            static bool once = true;
            if (once) { std::printf("  scene framebuffer binding = %d\n", fb); once = false; }
        }
        for (size_t i = 0; i < meshes.size(); i++) {
            if (!r.visible(chunks[i].mn, chunks[i].mx)) continue;
            r.draw(meshes[i], mat4(), vec3(1, 1, 1), 0.0f);
        }
        r.endScene();
        // Diagnostic: paint the target red first. If the frame comes back red
        // the post pass never drew; if it comes back black it drew black.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        r.postProcess(0.2f, 1.0f, 0.0f, 0.0f, 1.15f);
        GLenum e = glGetError();
        if (e != GL_NO_ERROR) std::printf("  GL error after postProcess: 0x%x\n", e);
        glFinish();
        return readFrame();
    };

    std::printf("\n[frame] torch on, standing at the spawn\n");
    float bestMean = 0.0f, bestLit = 0.0f;
    int bestLevels = 0;
    // Sweep the yaw: the spawn might happen to face a blank wall, and a test
    // that fails on where the player is looking is a flaky test.
    for (int i = 0; i < 8; i++) {
        float yaw = i * TAU / 8.0f;
        FrameStats fs = renderOnce(gameLikeScene(w, eye, yaw, true, false));
        bestMean = std::max(bestMean, fs.meanLuma);
        bestLit = std::max(bestLit, fs.litFraction);
        bestLevels = std::max(bestLevels, fs.distinctLevels);
    }
    std::printf("  best over 8 directions: mean luma %.4f, lit %.1f%%, %d distinct levels\n",
                bestMean, bestLit * 100.0f, bestLevels);

    // The bug this test exists for produced a mean luma of exactly 0.
    check(bestMean > 0.010f, "the world is not pitch black with the torch on");
    check(bestLit > 0.06f, "a meaningful fraction of the frame is actually visible");
    check(bestLevels >= 6, "the frame has real tonal range, not one flat colour");

    std::printf("\n[frame] torch off\n");
    FrameStats dark = renderOnce(gameLikeScene(w, eye, 0.0f, false, false));
    std::printf("  mean luma %.4f, lit %.1f%%\n", dark.meanLuma, dark.litFraction * 100.0f);
    // If this were not darker than torch-on, the torch would not be doing
    // anything and the test above would be passing for the wrong reason.
    check(dark.meanLuma < bestMean, "the torch measurably lights the scene");

    std::printf("\n[frame] indoors\n");
    vec3 inside = eye;
    for (const Building& b : w.buildings) {
        if (!b.enterable || b.doorCX < 0) continue;
        inside = w.cellCenter(b.doorCX, b.doorCZ) + vec3(0, 1.66f, 0);
        break;
    }
    FrameStats in = renderOnce(gameLikeScene(w, inside, 0.0f, true, true));
    std::printf("  mean luma %.4f, lit %.1f%%\n", in.meanLuma, in.litFraction * 100.0f);
    check(in.meanLuma > 0.008f, "building interiors are not pitch black");

    std::printf("\n[frame] the creature is visible when lit\n");
    {
        Monster m;
        m.spawn(w, w.playerStart, AK_STALKER, 5u);
        // Put it directly in front of the camera, four metres away.
        float yaw = 0.0f;
        vec3 ahead = w.playerStart + vec3(std::sin(yaw), 0, std::cos(yaw)) * 4.0f;
        vec3 open;
        Rng rng(1u);
        if (w.findOpenNear(ahead, 6.0f, open, rng)) m.pos = open;
        m.yaw = yaw + PI;
        m.state = AS_HUNT;
        m.buildPose(0.0f);

        SceneParams sp = gameLikeScene(w, eye, yaw, true, false);
        r.beginShadowPass(sp);
        for (size_t i = 0; i < meshes.size(); i++) {
            vec3 c = (chunks[i].mn + chunks[i].mx) * 0.5f;
            if (length(vec3(c.x - sp.camPos.x, 0, c.z - sp.camPos.z)) > 40.0f) continue;
            r.drawShadow(meshes[i], mat4());
        }
        for (int p = 0; p < BP_COUNT; p++) r.drawShadow(actor[p], m.parts[p]);
        r.endShadowPass();

        r.beginScene(sp);
        for (size_t i = 0; i < meshes.size(); i++) {
            if (!r.visible(chunks[i].mn, chunks[i].mx)) continue;
            r.draw(meshes[i], mat4(), vec3(1, 1, 1), 0.0f);
        }
        FrameStats without = readFrame();
        for (int p = 0; p < BP_COUNT; p++)
            r.draw(actor[p], m.parts[p], vec3(0.82f, 0.78f, 0.76f), 0.012f);
        r.endScene();
        r.postProcess(0.2f, 1.0f, 0.0f, 0.0f, 1.15f);
        glFinish();
        FrameStats with = readFrame();
        std::printf("  scene alone %.4f, with the creature %.4f\n",
                    without.meanLuma, with.meanLuma);
        check(with.meanLuma != without.meanLuma, "drawing the creature changes the picture");
        check(glGetError() == GL_NO_ERROR, "no GL error after drawing an actor");
    }

    std::printf("\n[fallback] a missing pack must not produce a black world\n");
    {
        Renderer r2;
        check(r2.init(nullptr), "the renderer starts even with no asset pack");
        r2.resize(FBW, FBH);
        r2.setRenderScale(1.0f);
        SceneParams sp = gameLikeScene(w, eye, 0.0f, true, false);
        r2.beginShadowPass(sp);
        r2.endShadowPass();
        r2.beginScene(sp);
        for (size_t i = 0; i < meshes.size(); i++) {
            if (!r2.visible(chunks[i].mn, chunks[i].mx)) continue;
            r2.draw(meshes[i], mat4(), vec3(1, 1, 1), 0.0f);
        }
        r2.endScene();
        r2.postProcess(0.2f, 1.0f, 0.0f, 0.0f, 1.15f);
        glFinish();
        FrameStats fs = readFrame();
        std::printf("  mean luma %.4f, lit %.1f%%\n", fs.meanLuma, fs.litFraction * 100.0f);
        check(fs.meanLuma > 0.010f, "the placeholder materials render a visible world");
        r2.shutdown();
    }

    for (auto& m : meshes) m.destroy();
    for (int i = 0; i < BP_COUNT; i++) actor[i].destroy();
    r.shutdown();

    std::printf("\n=== %d checks, %d failures ===\n", gChecks, gFails);
    return gFails ? 1 : 0;
}
