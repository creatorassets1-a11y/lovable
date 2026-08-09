// Offscreen preview harness.
//
// Builds the real renderer against a surfaceless EGL/GLES3 context (llvmpipe on
// a build machine) and writes frames to PNG. This is the only way to actually
// look at the graphics without a phone in hand, and it runs the same C++ that
// ships in the APK — not a reimplementation.
//
//   ./matron_preview <asset-dir> <out-dir> [shot ...]

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "../android/app/src/main/cpp/core/asset.h"
#include "../android/app/src/main/cpp/core/hmath.h"
#include "../android/app/src/main/cpp/game/level.h"
#include "../android/app/src/main/cpp/gfx/renderer.h"

#include "stb_write_shim.h"

using namespace hl;
using namespace hm;

static const int WIDTH = 1280;
static const int HEIGHT = 720;

static bool init_egl(EGLDisplay* out_dpy, EGLContext* out_ctx) {
    auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
        eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = EGL_NO_DISPLAY;
    if (getPlatformDisplay) {
        dpy = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    }
    if (dpy == EGL_NO_DISPLAY) dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) { std::fprintf(stderr, "no EGL display\n"); return false; }

    EGLint major, minor;
    if (!eglInitialize(dpy, &major, &minor)) {
        std::fprintf(stderr, "eglInitialize failed\n");
        return false;
    }
    eglBindAPI(EGL_OPENGL_ES_API);

    const EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE,
    };
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1) {
        std::fprintf(stderr, "no EGL config\n");
        return false;
    }
    const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT) { std::fprintf(stderr, "no EGL context\n"); return false; }
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        std::fprintf(stderr, "eglMakeCurrent failed\n");
        return false;
    }
    *out_dpy = dpy;
    *out_ctx = ctx;
    std::printf("GL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    return true;
}

/** The preview renders into an FBO, since a surfaceless context has no window. */
struct Backbuffer {
    GLuint fbo = 0, color = 0, depth = 0;
    bool create(int w, int h) {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
};

static void save_png(const std::string& path, int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    write_png_flipped(path.c_str(), w, h, px.data());
    std::printf("  wrote %s\n", path.c_str());
}

struct MaterialSet {
    Texture a, n, orm;
    Material mat;
    void load(const std::string& name, float uv, bool triplanar = false) {
        a.load("tex/" + name + "_a.png", true);
        n.load("tex/" + name + "_n.png", false);
        orm.load("tex/" + name + "_orm.png", false);
        mat.albedo = &a;
        mat.normal = &n;
        mat.orm = &orm;
        mat.uv_scale = v2(uv, uv);
        mat.triplanar = triplanar;
    }
};

int main(int argc, char** argv) {
    const std::string asset_dir = argc > 1 ? argv[1] : "android/app/src/main/assets";
    const std::string out_dir = argc > 2 ? argv[2] : "dist/shots3d";

    EGLDisplay dpy;
    EGLContext ctx;
    if (!init_egl(&dpy, &ctx)) return 1;

    Backbuffer bb;
    if (!bb.create(WIDTH, HEIGHT)) { std::fprintf(stderr, "backbuffer failed\n"); return 1; }

    asset_set_directory(asset_dir);

    Renderer renderer;
    if (!renderer.init(WIDTH, HEIGHT)) { std::fprintf(stderr, "renderer init failed\n"); return 1; }
    renderer.set_output_framebuffer(bb.fbo);

    Level level;
    if (!level.load("level/ward.txt")) return 1;

    MaterialSet m_wall, m_floor, m_ceil, m_metal, m_fabric, m_skin, m_child, m_rubble, m_plaster;
    m_wall.load("wall_tile", 1.0f);
    m_plaster.load("wall_plaster", 1.0f);
    m_floor.load("floor_lino", 1.0f);
    m_ceil.load("ceiling", 1.0f);
    m_metal.load("metal", 1.2f);
    m_fabric.load("fabric", 1.5f);
    m_rubble.load("rubble", 1.0f);
    m_skin.load("skin_burn", 2.8f, true);
    m_child.load("skin_child", 2.2f, true);

    Mesh matron, child, bed, locker, gurney, ivstand, wheelchair, lamp, debris;
    matron.load("mesh/matron.hmsh");
    child.load("mesh/child.hmsh");
    bed.load("mesh/bed.hmsh");
    locker.load("mesh/locker.hmsh");
    gurney.load("mesh/gurney.hmsh");
    ivstand.load("mesh/ivstand.hmsh");
    wheelchair.load("mesh/wheelchair.hmsh");
    lamp.load("mesh/lamp.hmsh");
    debris.load("mesh/debris.hmsh");

    Animator anim_matron, anim_child;
    anim_matron.init(&matron.bones(), &matron.clips());
    anim_child.init(&child.bones(), &child.clips());

    // ---- scene assembly
    std::vector<DrawItem> items;
    auto push = [&](const Mesh* mesh, const Material* mat, v3 pos, float yaw = 0,
                    const Animator* an = nullptr) {
        DrawItem it;
        it.mesh = mesh;
        it.material = mat;
        it.model = mat4_translate(pos) * mat4_rotate_y(yaw);
        it.animator = an;
        items.push_back(it);
    };

    items.push_back({&level.walls(), mat4_identity(), &m_wall.mat, nullptr, true});
    items.push_back({&level.floor(), mat4_identity(), &m_floor.mat, nullptr, false});
    items.push_back({&level.ceiling(), mat4_identity(), &m_ceil.mat, nullptr, true});
    items.push_back({&level.trim(), mat4_identity(), &m_plaster.mat, nullptr, true});

    Rng rng(1234);
    for (const auto& s : level.spawns()) {
        v3 p = level.cell_center(s.x, s.z);
        float yaw = rng.range(0, TAU);
        switch (s.tag) {
            case 'B': push(&bed, &m_fabric.mat, p, std::round(yaw / (PI * 0.5f)) * PI * 0.5f); break;
            case 'L': push(&locker, &m_metal.mat, p, std::round(yaw / (PI * 0.5f)) * PI * 0.5f); break;
            case 'G': push(&gurney, &m_metal.mat, p, yaw); break;
            case 'I': push(&ivstand, &m_metal.mat, p, yaw); break;
            case 'W': push(&wheelchair, &m_metal.mat, p, yaw); break;
            case 'R': push(&debris, &m_rubble.mat, p, yaw); break;
            case 'A': push(&lamp, &m_metal.mat, p + v3(0, WALL_H - 0.02f, 0), 0); break;
            default: break;
        }
    }

    v3 matron_pos(0, 0, 0), child_pos(0, 0, 0), player_pos(0, 0, 0);
    level.find_spawn('M', &matron_pos);
    level.find_spawn('P', &player_pos);
    auto children = level.all_spawns('C');
    if (!children.empty()) child_pos = children[0];

    // ---- shots. Each is a camera pose plus the state of the two characters.
    struct Shot {
        const char* name;
        v3 eye;
        float yaw;
        float pitch;
        v3 matron;
        float matron_yaw;
        const char* clip;
        float anim_time;
    };

    v3 corridor(9.0f, 1.65f, 27.0f);
    std::vector<Shot> shots = {
        {"corridor",   corridor,                      0.0f,   -0.02f, v3(30, 0, 27), PI,      "walk",   0.7f},
        {"approach",   v3(21, 1.65f, 27),             0.0f,   -0.01f, v3(31, 0, 27), PI,      "walk",   1.1f},
        {"close",      v3(26, 1.62f, 27),             0.0f,    0.06f, v3(29.6f, 0, 27.2f), PI, "listen", 1.4f},
        {"face",       v3(28.2f, 1.70f, 27.0f),       0.0f,    0.10f, v3(29.8f, 0, 27.0f), PI, "scream", 0.5f},
        {"ward_room",  v3(15.5f, 1.65f, 38.0f),      -1.2f,   -0.04f, v3(14.0f, 0, 34.0f), 0.4f, "idle",  2.0f},
        {"chase",      v3(35, 1.60f, 27),             PI,      0.02f, v3(31.5f, 0, 27), 0.0f,  "chase",  0.3f},
    };

    std::vector<std::string> want;
    bool no_tri = false, debug_normals = false, glow = false;
    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--notri") { no_tri = true; continue; }
        if (a == "--normals") { debug_normals = true; continue; }
        if (a == "--glow") { glow = true; continue; }
        want.push_back(a);
    }
    if (no_tri) { m_skin.mat.triplanar = false; m_child.mat.triplanar = false; }
    if (glow) { m_skin.mat.emissive = 1.0f; m_child.mat.emissive = 1.0f; }
    if (debug_normals) {
        // Flat white, flat normal, mid roughness: whatever shading remains is
        // entirely down to the lighting and the normal pipeline.
        m_skin.a.make_solid(0xFFFFFFFFu);
        m_skin.n.make_solid(0xFFFF8080u);
        m_skin.orm.make_solid(0xFF0080FFu);
    }

    for (const auto& s : shots) {
        if (!want.empty() &&
            std::find(want.begin(), want.end(), s.name) == want.end()) continue;

        anim_matron.play(s.clip, 0.0f, true);
        // Step the animator so blending and the pose settle at the sample time.
        anim_matron.update(0.0f);
        for (float t = 0; t < s.anim_time; t += 1.0f / 60.0f) anim_matron.update(1.0f / 60.0f);
        anim_child.play("crawl", 0.0f, true);
        for (float t = 0; t < 0.9f; t += 1.0f / 60.0f) anim_child.update(1.0f / 60.0f);

        std::vector<DrawItem> frame = items;
        DrawItem m;
        m.mesh = &matron;
        m.material = &m_skin.mat;
        m.model = mat4_translate(s.matron) * mat4_rotate_y(s.matron_yaw);
        m.animator = &anim_matron;
        frame.push_back(m);

        DrawItem c;
        c.mesh = &child;
        c.material = &m_child.mat;
        c.model = mat4_translate(child_pos) * mat4_rotate_y(1.2f);
        c.animator = &anim_child;
        frame.push_back(c);

        SceneView view;
        v3 fwd(std::sin(s.yaw) * std::cos(s.pitch), std::sin(s.pitch),
               std::cos(s.yaw) * std::cos(s.pitch));
        // Level +Z runs along the corridor; yaw 0 looks down it.
        fwd = normalize(v3(std::cos(s.yaw), std::sin(s.pitch), std::sin(s.yaw)));
        view.cam_pos = s.eye;
        view.view = mat4_look_at(s.eye, s.eye + fwd, v3(0, 1, 0));
        view.proj = mat4_perspective(radians(68.0f), (float)WIDTH / HEIGHT, 0.05f, 60.0f);

        // Torch rides slightly below and right of the eye, like a held light.
        view.torch_pos = s.eye + v3(0.10f, -0.12f, 0.0f);
        view.torch_dir = fwd;
        view.torch_intensity = 30.0f;
        view.torch_range = 20.0f;

        view.ambient = v3(0.014f, 0.016f, 0.022f);
        view.fog_color = v3(0.013f, 0.014f, 0.019f);
        view.fog_density = 0.048f;

        // A couple of failing emergency lamps for depth cueing.
        view.points.push_back({v3(13.0f, 2.7f, 27.0f), 7.0f, v3(0.9f, 0.35f, 0.18f), 0.5f});
        view.points.push_back({v3(41.0f, 2.7f, 27.0f), 8.0f, v3(0.55f, 0.62f, 0.9f), 0.35f});

        PostParams post;
        post.exposure = 1.15f;
        post.bloom = 0.65f;
        post.grain = 0.05f;
        post.vignette = 0.35f;
        if (std::strcmp(s.name, "face") == 0) {
            post.chroma = 0.5f;
            post.red_shift = 0.25f;
            post.exposure = 1.4f;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, bb.fbo);
        glViewport(0, 0, WIDTH, HEIGHT);
        renderer.render(view, frame, post, 3.7f);

        // The renderer's composite targets the default framebuffer; in the
        // harness that is the preview FBO, which is already bound.
        glBindFramebuffer(GL_FRAMEBUFFER, bb.fbo);
        save_png(out_dir + "/" + s.name + ".png", WIDTH, HEIGHT);
    }

    std::printf("draw calls %d, triangles %d\n",
                renderer.last_draw_calls(), renderer.last_triangles());
    return 0;
}
