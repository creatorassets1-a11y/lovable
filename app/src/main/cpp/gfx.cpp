#include "gfx.h"
#include "noise.h"
#include "font.h"
#include <android/log.h>
#include <cstring>
#include <cstdlib>
#include <vector>

#define GLOG(...) __android_log_print(ANDROID_LOG_INFO, "HollowGfx", __VA_ARGS__)
#define GERR(...) __android_log_print(ANDROID_LOG_ERROR, "HollowGfx", __VA_ARGS__)

namespace hm {

// ------------------------------------------------------------------- shaders

static const char* kSceneVS = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat4 uLightVP;
out vec3 vWorld;
out vec3 vNormal;
out vec2 vUV;
out vec4 vLightPos;
void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorld = wp.xyz;
    vNormal = mat3(uModel) * aNormal;
    vUV = aUV;
    vLightPos = uLightVP * wp;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static const char* kSceneFS = R"(#version 300 es
precision mediump float;
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;
in vec4 vLightPos;

uniform sampler2D uTexA;
uniform sampler2D uTexB;
uniform sampler2D uShadow;
uniform vec3 uCamPos;
uniform vec3 uTint;
uniform float uBlendByNormal;
uniform float uEmissive;
uniform vec3 uTorchPos;
uniform vec3 uTorchDir;
uniform vec3 uTorchColor;
uniform vec4 uTorchParams;   // innerCos, outerCos, range, intensity
uniform int  uNumLights;
uniform vec3 uLightPos[8];
uniform vec3 uLightColor[8];
uniform float uLightRadius[8];
uniform vec3 uAmbient;
uniform vec3 uFogColor;
uniform float uFogDensity;
out vec4 fragColor;

float shadowFactor(vec3 N, vec3 L) {
    vec3 p = vLightPos.xyz / vLightPos.w;
    p = p * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;
    // Slope-scaled bias: without it, floors self-shadow into stripes.
    float bias = max(0.0045 * (1.0 - dot(N, L)), 0.0015);
    float sum = 0.0;
    vec2 texel = vec2(1.0 / 1024.0);
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float d = texture(uShadow, p.xy + vec2(float(x), float(y)) * texel).r;
            sum += (p.z - bias > d) ? 0.0 : 1.0;
        }
    }
    return sum / 9.0;
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 ca = texture(uTexA, vUV).rgb;
    vec3 cb = texture(uTexB, vUV).rgb;
    // Horizontal surfaces get the floor texture, vertical ones the wall
    // texture. One mesh, two materials, no extra draw calls.
    float k = uBlendByNormal * smoothstep(0.45, 0.72, abs(N.y));
    vec3 albedo = mix(ca, cb, k) * uTint;

    vec3 V = normalize(uCamPos - vWorld);
    vec3 col = uAmbient * albedo;

    vec3 Ld = uTorchPos - vWorld;
    float dist = length(Ld);
    vec3 L = Ld / max(dist, 0.0001);
    float spot = dot(-L, normalize(uTorchDir));
    float cone = smoothstep(uTorchParams.y, uTorchParams.x, spot);
    float atten = clamp(1.0 - dist / uTorchParams.z, 0.0, 1.0);
    atten *= atten;
    float ndl = max(dot(N, L), 0.0);
    float lit = cone * atten * uTorchParams.w;
    if (lit > 0.001) {
        float sh = shadowFactor(N, L);
        col += albedo * uTorchColor * (ndl * lit * sh);
        vec3 H = normalize(L + V);
        col += uTorchColor * pow(max(dot(N, H), 0.0), 28.0) * lit * sh * 0.20;
    }

    for (int i = 0; i < 8; i++) {
        if (i >= uNumLights) break;
        vec3 d = uLightPos[i] - vWorld;
        float dd = length(d);
        float a = clamp(1.0 - dd / max(uLightRadius[i], 0.001), 0.0, 1.0);
        a *= a;
        if (a <= 0.001) continue;
        col += albedo * uLightColor[i] * (max(dot(N, d / max(dd, 0.0001)), 0.0) * a);
    }

    col += albedo * uEmissive;

    float fd = length(uCamPos - vWorld) * uFogDensity;
    float fog = 1.0 - exp(-fd * fd);
    col = mix(col, uFogColor, clamp(fog, 0.0, 1.0));
    fragColor = vec4(col, 1.0);
}
)";

static const char* kDepthVS = R"(#version 300 es
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); }
)";

static const char* kDepthFS = R"(#version 300 es
precision mediump float;
void main() { }
)";

static const char* kPostVS = R"(#version 300 es
out vec2 vUV;
void main() {
    // Fullscreen triangle from gl_VertexID: no vertex buffer needed.
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    vUV = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

static const char* kPostFS = R"(#version 300 es
precision mediump float;
in vec2 vUV;
uniform sampler2D uTex;
uniform float uFear;
uniform float uTime;
uniform float uFade;
uniform float uDamage;
uniform float uAspect;
out vec4 fragColor;

float hash(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}

void main() {
    vec2 uv = vUV;

    // Horizontal tearing that only appears when fear is high. Subtle enough
    // that players report "something felt wrong" rather than "there's a glitch".
    float tearAmt = smoothstep(0.55, 1.0, uFear);
    if (tearAmt > 0.0) {
        float band = floor(uv.y * 90.0);
        float n = hash(vec2(band, floor(uTime * 11.0)));
        if (n > 0.965) uv.x += (n - 0.982) * 0.10 * tearAmt;
    }

    // Breathing lens warp toward the centre - the walls feel like they lean in.
    vec2 c = uv - 0.5;
    float warp = (0.010 + 0.045 * uFear) * (0.5 + 0.5 * sin(uTime * 1.6));
    uv = 0.5 + c * (1.0 - warp * dot(c, c) * 4.0);

    // Chromatic aberration, radial, scaled by fear and damage.
    float ab = (0.0012 + 0.0075 * uFear + 0.012 * uDamage);
    vec2 dir = normalize(c + vec2(1e-6));
    float rr = texture(uTex, uv + dir * ab).r;
    float gg = texture(uTex, uv).g;
    float bb = texture(uTex, uv - dir * ab).b;
    vec3 col = vec3(rr, gg, bb);

    // Fear desaturates and cools everything; damage pushes it red.
    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(col, vec3(lum) * vec3(0.82, 0.88, 1.06), uFear * 0.55);
    col = mix(col, vec3(lum * 1.6, lum * 0.12, lum * 0.10), uDamage * 0.8);

    // Grain: heavier in the dark, which is where real sensors are noisy too.
    float g = hash(uv * vec2(1024.0, 1024.0) + fract(uTime) * 77.7);
    col += (g - 0.5) * (0.030 + 0.075 * uFear) * (1.2 - lum);

    // Vignette closes in as fear rises.
    float d = length(vec2(c.x * uAspect, c.y));
    float vig = smoothstep(1.05, 0.28 - uFear * 0.10, d);
    col *= mix(1.0, vig, 0.55 + 0.40 * uFear);

    // Slow scanline roll, very low amplitude.
    col *= 1.0 - 0.030 * sin(uv.y * 900.0 + uTime * 3.0) * (0.3 + uFear);

    col *= (1.0 - uFade);
    fragColor = vec4(col, 1.0);
}
)";

static const char* kUiVS = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aCol;
uniform vec2 uScreen;
out vec2 vUV;
out vec4 vCol;
void main() {
    vUV = aUV;
    vCol = aCol;
    vec2 p = vec2(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

static const char* kUiFS = R"(#version 300 es
precision mediump float;
in vec2 vUV;
in vec4 vCol;
uniform sampler2D uUiTex;
out vec4 fragColor;
void main() {
    vec4 t = texture(uUiTex, vUV);
    fragColor = vec4(vCol.rgb, vCol.a * t.a);
}
)";

// ----------------------------------------------------------------- utilities

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        GERR("shader compile failed: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link(const char* vs, const char* fs) {
    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        GERR("program link failed: %s", log);
        glDeleteProgram(p);
        p = 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

// ------------------------------------------------------------------ GpuMesh

void GpuMesh::upload(const Mesh& m) {
    if (m.verts.empty() || m.idx.empty()) { count = 0; return; }
    if (!vao) glGenVertexArrays(1, &vao);
    if (!vbo) glGenBuffers(1, &vbo);
    if (!ibo) glGenBuffers(1, &ibo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, m.verts.size() * sizeof(Vertex), m.verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m.idx.size() * sizeof(uint16_t), m.idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(6 * sizeof(float)));
    glBindVertexArray(0);
    count = (GLsizei)m.idx.size();
}

void GpuMesh::destroy() {
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ibo) glDeleteBuffers(1, &ibo);
    if (vao) glDeleteVertexArrays(1, &vao);
    vbo = ibo = vao = 0;
    count = 0;
}

// ------------------------------------------------------------ texture baking

static const int TEXSZ = 256;

static void writePixel(std::vector<uint8_t>& d, int x, int y, float r, float g, float b) {
    int i = (y * TEXSZ + x) * 4;
    d[i + 0] = (uint8_t)(clampf(r, 0.0f, 1.0f) * 255.0f);
    d[i + 1] = (uint8_t)(clampf(g, 0.0f, 1.0f) * 255.0f);
    d[i + 2] = (uint8_t)(clampf(b, 0.0f, 1.0f) * 255.0f);
    d[i + 3] = 255;
}

static GLuint makeTexture(const std::vector<uint8_t>& data) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TEXSZ, TEXSZ, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return t;
}

void Renderer::buildTextures() {
    std::vector<uint8_t> d(TEXSZ * TEXSZ * 4);
    const float inv = 8.0f / TEXSZ;   // 8 noise cells across the tile

    // --- concrete wall: cast marks, pitting, damp streaks running down ---
    for (int y = 0; y < TEXSZ; y++) {
        for (int x = 0; x < TEXSZ; x++) {
            float fx = x * inv, fy = y * inv;
            float base = 0.34f + fbm(fx, fy, 4, 8) * 0.30f;
            float pits = worley(fx * 2.0f, fy * 2.0f, 16);
            base -= smoothstepf(0.35f, 0.0f, pits) * 0.22f;
            // Vertical damp streaks: sampled with a squashed y so they smear down.
            float streak = fbm(fx * 3.0f, fy * 0.35f, 3, 24);
            base -= smoothstepf(0.55f, 0.85f, streak) * 0.20f;
            float grain = (hash2(x * 7, y * 13) - 0.5f) * 0.045f;
            base += grain;
            writePixel(d, x, y, base * 1.00f, base * 0.985f, base * 0.94f);
        }
    }
    mTex[TEX_WALL] = makeTexture(d);

    // --- floor: darker, tiled, with grime pooling in the joints ---
    for (int y = 0; y < TEXSZ; y++) {
        for (int x = 0; x < TEXSZ; x++) {
            float fx = x * inv, fy = y * inv;
            float base = 0.20f + fbm(fx * 1.4f, fy * 1.4f, 4, 12) * 0.22f;
            // Tile grid every 64px, with joints darkened.
            float gx = std::fabs(std::fmod((float)x, 64.0f) - 32.0f) / 32.0f;
            float gy = std::fabs(std::fmod((float)y, 64.0f) - 32.0f) / 32.0f;
            float joint = std::max(smoothstepf(0.90f, 1.0f, gx), smoothstepf(0.90f, 1.0f, gy));
            base -= joint * 0.13f;
            float grime = fbm(fx * 0.8f, fy * 0.8f, 3, 8);
            base *= 0.75f + grime * 0.5f;
            base += (hash2(x * 3, y * 5) - 0.5f) * 0.035f;
            writePixel(d, x, y, base * 0.98f, base * 0.97f, base * 0.92f);
        }
    }
    mTex[TEX_FLOOR] = makeTexture(d);

    // --- metal: brushed, with rust blooming out of the low spots ---
    for (int y = 0; y < TEXSZ; y++) {
        for (int x = 0; x < TEXSZ; x++) {
            float fx = x * inv, fy = y * inv;
            float brushed = 0.36f + fbm(fx * 6.0f, fy * 0.25f, 3, 32) * 0.26f;
            float rustMask = fbm(fx * 1.2f, fy * 1.2f, 4, 10);
            float rust = smoothstepf(0.52f, 0.78f, rustMask);
            float r = lerpf(brushed, 0.34f, rust);
            float g = lerpf(brushed * 1.01f, 0.17f, rust);
            float b = lerpf(brushed * 1.06f, 0.09f, rust);
            float grain = (hash2(x * 11, y * 3) - 0.5f) * 0.05f;
            writePixel(d, x, y, r + grain, g + grain, b + grain);
        }
    }
    mTex[TEX_METAL] = makeTexture(d);

    // --- flesh: cellular, veined, drained of colour ---
    for (int y = 0; y < TEXSZ; y++) {
        for (int x = 0; x < TEXSZ; x++) {
            float fx = x * inv, fy = y * inv;
            float cells = worley(fx * 3.0f, fy * 3.0f, 24);
            float skin = 0.42f + smoothstepf(0.0f, 0.5f, cells) * 0.22f;
            // Veins: thin dark filaments where fbm crosses a narrow band.
            float vn = fbm(fx * 2.2f, fy * 2.2f, 4, 16);
            float vein = 1.0f - smoothstepf(0.0f, 0.055f, std::fabs(vn - 0.47f));
            skin -= vein * 0.24f;
            float mottle = fbm(fx * 5.0f, fy * 5.0f, 3, 40);
            skin *= 0.86f + mottle * 0.28f;
            writePixel(d, x, y, skin * 0.86f, skin * 0.80f, skin * 0.78f);
        }
    }
    mTex[TEX_FLESH] = makeTexture(d);
}

// The atlas is 16x5 cells of 8x8. Glyphs occupy cells 0..63; cell 79 is a
// solid white block so shapes and text share one texture and one draw call.
static const int ATLAS_COLS = 16, ATLAS_ROWS = 5, ATLAS_CELL = 8;
static const int ATLAS_W = ATLAS_COLS * ATLAS_CELL;   // 128
static const int ATLAS_H = ATLAS_ROWS * ATLAS_CELL;   // 40
static const int WHITE_CELL = 79;

void Renderer::buildFont() {
    std::vector<uint8_t> d(ATLAS_W * ATLAS_H * 4, 0);
    for (int i = 0; i < ATLAS_W * ATLAS_H; i++) {
        d[i * 4 + 0] = 255; d[i * 4 + 1] = 255; d[i * 4 + 2] = 255; d[i * 4 + 3] = 0;
    }
    for (int gi = 0; gi < FONT_COUNT; gi++) {
        int col = gi % ATLAS_COLS, row = gi / ATLAS_COLS;
        for (int y = 0; y < FONT_H; y++) {
            uint8_t bits = kFont[gi][y];
            for (int x = 0; x < FONT_W; x++) {
                if (!(bits & (1 << x))) continue;
                int px = col * ATLAS_CELL + x;
                int py = row * ATLAS_CELL + y;
                d[(py * ATLAS_W + px) * 4 + 3] = 255;
            }
        }
    }
    int wc = WHITE_CELL % ATLAS_COLS, wr = WHITE_CELL / ATLAS_COLS;
    for (int y = 0; y < ATLAS_CELL; y++)
        for (int x = 0; x < ATLAS_CELL; x++)
            d[((wr * ATLAS_CELL + y) * ATLAS_W + (wc * ATLAS_CELL + x)) * 4 + 3] = 255;

    glGenTextures(1, &mFontTex);
    glBindTexture(GL_TEXTURE_2D, mFontTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ATLAS_W, ATLAS_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, d.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// ---------------------------------------------------------------- lifecycle

// Android destroys the EGL context whenever the window goes away, which takes
// every GL object with it. init() runs again on the way back, so anything
// still holding a name from the old context has to be cleared first or we
// hand the driver stale IDs and get a black screen (or worse).
void Renderer::forgetGlState() {
    mSceneFbo = mSceneColor = mSceneDepth = 0;
    mShadowFbo = mShadowTex = 0;
    for (int i = 0; i < TEX_COUNT; i++) mTex[i] = 0;
    mFontTex = 0;
    mUiVao = mUiVbo = mEmptyVao = 0;
    mSceneProg = mDepthProg = mPostProg = mUiProg = 0;
    mUiCount = 0;
    std::free(mUiVerts);
    mUiVerts = nullptr;
}

bool Renderer::init() {
    forgetGlState();
    mSceneProg = link(kSceneVS, kSceneFS);
    mDepthProg = link(kDepthVS, kDepthFS);
    mPostProg  = link(kPostVS, kPostFS);
    mUiProg    = link(kUiVS, kUiFS);
    if (!mSceneProg || !mDepthProg || !mPostProg || !mUiProg) {
        GERR("shader setup failed");
        return false;
    }

    uMVP = glGetUniformLocation(mSceneProg, "uMVP");
    uModel = glGetUniformLocation(mSceneProg, "uModel");
    uLightVP = glGetUniformLocation(mSceneProg, "uLightVP");
    uCamPos = glGetUniformLocation(mSceneProg, "uCamPos");
    uTint = glGetUniformLocation(mSceneProg, "uTint");
    uTexA = glGetUniformLocation(mSceneProg, "uTexA");
    uTexB = glGetUniformLocation(mSceneProg, "uTexB");
    uShadow = glGetUniformLocation(mSceneProg, "uShadow");
    uBlendN = glGetUniformLocation(mSceneProg, "uBlendByNormal");
    uEmissive = glGetUniformLocation(mSceneProg, "uEmissive");
    uTorchPos = glGetUniformLocation(mSceneProg, "uTorchPos");
    uTorchDir = glGetUniformLocation(mSceneProg, "uTorchDir");
    uTorchColor = glGetUniformLocation(mSceneProg, "uTorchColor");
    uTorchParams = glGetUniformLocation(mSceneProg, "uTorchParams");
    uNumLights = glGetUniformLocation(mSceneProg, "uNumLights");
    uLightPosArr = glGetUniformLocation(mSceneProg, "uLightPos");
    uLightColArr = glGetUniformLocation(mSceneProg, "uLightColor");
    uLightRadArr = glGetUniformLocation(mSceneProg, "uLightRadius");
    uAmbient = glGetUniformLocation(mSceneProg, "uAmbient");
    uFogColor = glGetUniformLocation(mSceneProg, "uFogColor");
    uFogDensity = glGetUniformLocation(mSceneProg, "uFogDensity");

    dMVP = glGetUniformLocation(mDepthProg, "uMVP");

    pTex = glGetUniformLocation(mPostProg, "uTex");
    pFear = glGetUniformLocation(mPostProg, "uFear");
    pTime = glGetUniformLocation(mPostProg, "uTime");
    pFade = glGetUniformLocation(mPostProg, "uFade");
    pDamage = glGetUniformLocation(mPostProg, "uDamage");
    pAspect = glGetUniformLocation(mPostProg, "uAspect");

    uScreen = glGetUniformLocation(mUiProg, "uScreen");
    uUiTex = glGetUniformLocation(mUiProg, "uUiTex");

    buildTextures();
    buildFont();

    glGenVertexArrays(1, &mEmptyVao);

    mUiVerts = (UiVert*)std::malloc(sizeof(UiVert) * UI_MAX_VERTS);
    glGenVertexArrays(1, &mUiVao);
    glGenBuffers(1, &mUiVbo);
    glBindVertexArray(mUiVao);
    glBindBuffer(GL_ARRAY_BUFFER, mUiVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(UiVert) * UI_MAX_VERTS, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVert), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UiVert), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UiVert), (void*)(4 * sizeof(float)));
    glBindVertexArray(0);

    // Shadow map.
    glGenTextures(1, &mShadowTex);
    glBindTexture(GL_TEXTURE_2D, mShadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Clamp to a border of "fully lit" so geometry outside the map is not
    // spuriously shadowed. GLES3 has no border colour, so clamp-to-edge plus
    // the in-shader bounds check does the job.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &mShadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, mShadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mShadowTex, 0);
    GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        GERR("shadow fbo incomplete");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    GLOG("renderer ready: %s", (const char*)glGetString(GL_RENDERER));
    return true;
}

void Renderer::createTargets() {
    destroyTargets();
    mSceneW = std::max(1, (int)(mW * mScale));
    mSceneH = std::max(1, (int)(mH * mScale));

    glGenTextures(1, &mSceneColor);
    glBindTexture(GL_TEXTURE_2D, mSceneColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, mSceneW, mSceneH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenRenderbuffers(1, &mSceneDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, mSceneDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mSceneW, mSceneH);

    glGenFramebuffers(1, &mSceneFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, mSceneFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mSceneColor, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mSceneDepth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        GERR("scene fbo incomplete (%dx%d)", mSceneW, mSceneH);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::destroyTargets() {
    if (mSceneFbo) { glDeleteFramebuffers(1, &mSceneFbo); mSceneFbo = 0; }
    if (mSceneColor) { glDeleteTextures(1, &mSceneColor); mSceneColor = 0; }
    if (mSceneDepth) { glDeleteRenderbuffers(1, &mSceneDepth); mSceneDepth = 0; }
}

void Renderer::resize(int w, int h) {
    mW = std::max(1, w);
    mH = std::max(1, h);
    createTargets();
}

void Renderer::setRenderScale(float s) {
    float ns = clampf(s, 0.45f, 1.0f);
    if (std::fabs(ns - mScale) < 0.01f) return;
    mScale = ns;
    if (mW > 1) createTargets();
}

void Renderer::shutdown() {
    destroyTargets();
    if (mShadowFbo) glDeleteFramebuffers(1, &mShadowFbo);
    if (mShadowTex) glDeleteTextures(1, &mShadowTex);
    for (int i = 0; i < TEX_COUNT; i++) if (mTex[i]) glDeleteTextures(1, &mTex[i]);
    if (mFontTex) glDeleteTextures(1, &mFontTex);
    if (mUiVbo) glDeleteBuffers(1, &mUiVbo);
    if (mUiVao) glDeleteVertexArrays(1, &mUiVao);
    if (mEmptyVao) glDeleteVertexArrays(1, &mEmptyVao);
    if (mSceneProg) glDeleteProgram(mSceneProg);
    if (mDepthProg) glDeleteProgram(mDepthProg);
    if (mPostProg) glDeleteProgram(mPostProg);
    if (mUiProg) glDeleteProgram(mUiProg);
    std::free(mUiVerts);
    mUiVerts = nullptr;
    mShadowFbo = mShadowTex = mFontTex = mUiVbo = mUiVao = mEmptyVao = 0;
    mSceneProg = mDepthProg = mPostProg = mUiProg = 0;
}

// -------------------------------------------------------------- shadow pass

void Renderer::beginShadowPass(const SceneParams& sp) {
    // A perspective frustum aimed down the torch. Only what the torch can light
    // needs to cast, so the map covers exactly the beam's reach.
    float fov = std::acos(clampf(sp.torchOuterCos, -1.0f, 1.0f)) * 2.2f;
    fov = clampf(fov, 0.5f, 2.4f);
    mat4 lp = mat4::perspective(fov, 1.0f, 0.15f, sp.torchRange * 1.15f);
    vec3 up = std::fabs(sp.torchDir.y) > 0.95f ? vec3(1, 0, 0) : vec3(0, 1, 0);
    mat4 lv = mat4::lookAt(sp.torchPos, sp.torchPos + sp.torchDir, up);
    mLightVP = lp * lv;

    glBindFramebuffer(GL_FRAMEBUFFER, mShadowFbo);
    glViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(mDepthProg);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    // Front-face culling in the shadow pass pushes acne onto surfaces the
    // camera cannot see.
    glCullFace(GL_FRONT);
}

void Renderer::drawShadow(const GpuMesh& m, const mat4& model) {
    if (!m.valid()) return;
    mat4 mvp = mLightVP * model;
    glUniformMatrix4fv(dMVP, 1, GL_FALSE, mvp.m);
    glBindVertexArray(m.vao);
    glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_SHORT, nullptr);
}

void Renderer::endShadowPass() {
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// --------------------------------------------------------------- scene pass

void Renderer::beginScene(const SceneParams& sp) {
    glBindFramebuffer(GL_FRAMEBUFFER, mSceneFbo);
    glViewport(0, 0, mSceneW, mSceneH);
    glClearColor(sp.fogColor.x, sp.fogColor.y, sp.fogColor.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    mViewProj = sp.proj * sp.view;

    glUseProgram(mSceneProg);
    glUniformMatrix4fv(uLightVP, 1, GL_FALSE, mLightVP.m);
    glUniform3f(uCamPos, sp.camPos.x, sp.camPos.y, sp.camPos.z);
    glUniform3f(uTorchPos, sp.torchPos.x, sp.torchPos.y, sp.torchPos.z);
    glUniform3f(uTorchDir, sp.torchDir.x, sp.torchDir.y, sp.torchDir.z);
    glUniform3f(uTorchColor, sp.torchColor.x, sp.torchColor.y, sp.torchColor.z);
    glUniform4f(uTorchParams, sp.torchInnerCos, sp.torchOuterCos, sp.torchRange, sp.torchIntensity);
    glUniform3f(uAmbient, sp.ambient.x, sp.ambient.y, sp.ambient.z);
    glUniform3f(uFogColor, sp.fogColor.x, sp.fogColor.y, sp.fogColor.z);
    glUniform1f(uFogDensity, sp.fogDensity);

    int n = std::min(sp.numLights, MAX_POINT_LIGHTS);
    glUniform1i(uNumLights, n);
    if (n > 0) {
        float lp[MAX_POINT_LIGHTS * 3], lc[MAX_POINT_LIGHTS * 3], lr[MAX_POINT_LIGHTS];
        for (int i = 0; i < n; i++) {
            lp[i * 3 + 0] = sp.lightPos[i].x; lp[i * 3 + 1] = sp.lightPos[i].y; lp[i * 3 + 2] = sp.lightPos[i].z;
            lc[i * 3 + 0] = sp.lightColor[i].x; lc[i * 3 + 1] = sp.lightColor[i].y; lc[i * 3 + 2] = sp.lightColor[i].z;
            lr[i] = sp.lightRadius[i];
        }
        glUniform3fv(uLightPosArr, n, lp);
        glUniform3fv(uLightColArr, n, lc);
        glUniform1fv(uLightRadArr, n, lr);
    }

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, mShadowTex);
    glUniform1i(uShadow, 2);
    glUniform1i(uTexA, 0);
    glUniform1i(uTexB, 1);
}

void Renderer::draw(const GpuMesh& m, const mat4& model, const vec3& tint,
                    int texA, int texB, float blendByNormal, float emissive) {
    if (!m.valid()) return;
    mat4 mvp = mViewProj * model;
    glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(uModel, 1, GL_FALSE, model.m);
    glUniform3f(uTint, tint.x, tint.y, tint.z);
    glUniform1f(uBlendN, blendByNormal);
    glUniform1f(uEmissive, emissive);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mTex[texA]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mTex[texB]);
    glBindVertexArray(m.vao);
    glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_SHORT, nullptr);
}

void Renderer::endScene() {
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::postProcess(float fear, float time, float fade, float damage) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, mW, mH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(mPostProg);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mSceneColor);
    glUniform1i(pTex, 0);
    glUniform1f(pFear, fear);
    glUniform1f(pTime, time);
    glUniform1f(pFade, fade);
    glUniform1f(pDamage, damage);
    glUniform1f(pAspect, (float)mW / (float)std::max(1, mH));
    glBindVertexArray(mEmptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
}

// ------------------------------------------------------------------------ UI

static inline void whiteUV(float& u, float& v) {
    int wc = WHITE_CELL % ATLAS_COLS, wr = WHITE_CELL / ATLAS_COLS;
    u = (wc * ATLAS_CELL + 4.0f) / ATLAS_W;
    v = (wr * ATLAS_CELL + 4.0f) / ATLAS_H;
}

void Renderer::uiBegin() {
    mUiCount = 0;
}

void Renderer::uiPushQuad(float x0, float y0, float x1, float y1,
                          float u0, float v0, float u1, float v1,
                          float r, float g, float b, float a) {
    if (mUiCount + 6 > UI_MAX_VERTS) return;
    UiVert* p = mUiVerts + mUiCount;
    auto set = [&](int i, float x, float y, float u, float v) {
        p[i] = {x, y, u, v, r, g, b, a};
    };
    set(0, x0, y0, u0, v0);
    set(1, x1, y0, u1, v0);
    set(2, x1, y1, u1, v1);
    set(3, x0, y0, u0, v0);
    set(4, x1, y1, u1, v1);
    set(5, x0, y1, u0, v1);
    mUiCount += 6;
}

void Renderer::uiPushTri(float x0, float y0, float x1, float y1, float x2, float y2,
                         float r, float g, float b, float a) {
    if (mUiCount + 3 > UI_MAX_VERTS) return;
    float u, v;
    whiteUV(u, v);
    UiVert* p = mUiVerts + mUiCount;
    p[0] = {x0, y0, u, v, r, g, b, a};
    p[1] = {x1, y1, u, v, r, g, b, a};
    p[2] = {x2, y2, u, v, r, g, b, a};
    mUiCount += 3;
}

void Renderer::uiQuad(float x, float y, float w, float h, float r, float g, float b, float a) {
    float u, v;
    whiteUV(u, v);
    uiPushQuad(x, y, x + w, y + h, u, v, u, v, r, g, b, a);
}

void Renderer::uiDisc(float cx, float cy, float radius, float r, float g, float b, float a, int segments) {
    for (int i = 0; i < segments; i++) {
        float a0 = (float)i / segments * TAU;
        float a1 = (float)(i + 1) / segments * TAU;
        uiPushTri(cx, cy,
                  cx + std::cos(a0) * radius, cy + std::sin(a0) * radius,
                  cx + std::cos(a1) * radius, cy + std::sin(a1) * radius,
                  r, g, b, a);
    }
}

void Renderer::uiRing(float cx, float cy, float radius, float thickness,
                      float r, float g, float b, float a, int segments) {
    float inner = radius - thickness;
    for (int i = 0; i < segments; i++) {
        float a0 = (float)i / segments * TAU;
        float a1 = (float)(i + 1) / segments * TAU;
        float c0 = std::cos(a0), s0 = std::sin(a0);
        float c1 = std::cos(a1), s1 = std::sin(a1);
        uiPushTri(cx + c0 * inner, cy + s0 * inner,
                  cx + c0 * radius, cy + s0 * radius,
                  cx + c1 * radius, cy + s1 * radius, r, g, b, a);
        uiPushTri(cx + c0 * inner, cy + s0 * inner,
                  cx + c1 * radius, cy + s1 * radius,
                  cx + c1 * inner, cy + s1 * inner, r, g, b, a);
    }
}

float Renderer::textWidth(const char* s, float px) const {
    float sc = px / (float)FONT_H;
    int n = 0;
    for (const char* p = s; *p; p++) n++;
    if (n == 0) return 0.0f;
    return (n * 6.0f - 1.0f) * sc;
}

void Renderer::uiText(const char* s, float x, float y, float px,
                      float r, float g, float b, float a) {
    float sc = px / (float)FONT_H;
    float cw = FONT_W * sc, ch = FONT_H * sc;
    float cursor = x;
    for (const char* p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
        int gi = (int)c - FONT_FIRST;
        if (gi >= 0 && gi < FONT_COUNT && c != ' ') {
            int col = gi % ATLAS_COLS, row = gi / ATLAS_COLS;
            float u0 = (col * ATLAS_CELL) / (float)ATLAS_W;
            float u1 = (col * ATLAS_CELL + FONT_W) / (float)ATLAS_W;
            float v0 = (row * ATLAS_CELL) / (float)ATLAS_H;
            float v1 = (row * ATLAS_CELL + FONT_H) / (float)ATLAS_H;
            uiPushQuad(cursor, y, cursor + cw, y + ch, u0, v0, u1, v1, r, g, b, a);
        }
        cursor += 6.0f * sc;
    }
}

void Renderer::uiTextCentered(const char* s, float cx, float y, float px,
                              float r, float g, float b, float a) {
    uiText(s, cx - textWidth(s, px) * 0.5f, y, px, r, g, b, a);
}

void Renderer::uiFlush() {
    if (mUiCount == 0) return;
    glUseProgram(mUiProg);
    glUniform2f(uScreen, (float)mW, (float)mH);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mFontTex);
    glUniform1i(uUiTex, 0);
    glBindVertexArray(mUiVao);
    glBindBuffer(GL_ARRAY_BUFFER, mUiVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(UiVert) * mUiCount, mUiVerts);
    glDrawArrays(GL_TRIANGLES, 0, mUiCount);
    glBindVertexArray(0);
    mUiCount = 0;
}

void Renderer::uiEnd() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    uiFlush();
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

} // namespace hm
