// gfx.h - OpenGL ES 3.0 renderer.
//
// Forward lighting with one shadow-casting spotlight (your torch) plus up to
// eight cheap point lights. The torch shadow is the single most important
// effect in the game: it is what puts the creature's silhouette on the wall
// behind it before you have worked out what you are looking at.
#pragma once
#include "hmath.h"
#include "mesh.h"
#include "assets.h"
#include <GLES3/gl3.h>

namespace hm {

struct GpuMesh {
    GLuint vbo = 0, ibo = 0, vao = 0;
    GLsizei count = 0;
    void upload(const Mesh& m);
    void destroy();
    // Forget the handles without touching GL. Used after the context is lost:
    // the objects are already gone, and calling glDelete* on names from a dead
    // context is meaningless - but reusing them is actively harmful.
    void orphan() { vbo = ibo = vao = 0; count = 0; }
    bool valid() const { return count > 0; }
};

const int MAX_POINT_LIGHTS = 8;

struct SceneParams {
    mat4 view, proj;
    vec3 camPos;

    vec3 torchPos, torchDir, torchColor{1.0f, 0.94f, 0.82f};
    float torchInnerCos = 0.955f;
    float torchOuterCos = 0.84f;
    float torchRange = 20.0f;
    float torchIntensity = 1.0f;

    int numLights = 0;
    vec3 lightPos[MAX_POINT_LIGHTS];
    vec3 lightColor[MAX_POINT_LIGHTS];
    float lightRadius[MAX_POINT_LIGHTS];

    vec3 ambient{0.020f, 0.021f, 0.026f};
    vec3 fogColor{0.02f, 0.02f, 0.03f};
    float fogDensity = 0.09f;
    float time = 0.0f;
};

// Every material lives as a layer of one texture array, so a whole chunk of
// city - road, kerb, brick, glass, steel - is a single draw call.

class Renderer {
public:
    // The pack supplies the material array; without it the renderer falls back
    // to flat untextured surfaces rather than failing to start.
    bool init(const AssetPack* pack);
    void shutdown();
    void resize(int w, int h);
    int width() const { return mW; }
    int height() const { return mH; }
    // Below 1.0 the 3D scene renders to a smaller buffer and is upscaled by the
    // post pass. This is the knob that keeps weak GPUs at a playable framerate.
    void setRenderScale(float s);
    float renderScale() const { return mScale; }

    void beginShadowPass(const SceneParams& sp);
    void drawShadow(const GpuMesh& m, const mat4& model);
    void endShadowPass();

    void beginScene(const SceneParams& sp);
    void draw(const GpuMesh& m, const mat4& model, const vec3& tint, float emissive);
    void endScene();
    // Frustum test in world space, used to cull city chunks.
    bool visible(const vec3& mn, const vec3& mx) const;
    int drawCalls() const { return mDrawCalls; }
    int trisDrawn() const { return mTris; }

    // fear drives grain/aberration, fade is the black curtain, damage is the
    // red pulse when the creature reaches you.
    void postProcess(float fear, float time, float fade, float damage);

    void uiBegin();
    void uiQuad(float x, float y, float w, float h, float r, float g, float b, float a);
    void uiRing(float cx, float cy, float radius, float thickness,
                float r, float g, float b, float a, int segments = 28);
    void uiDisc(float cx, float cy, float radius, float r, float g, float b, float a, int segments = 24);
    void uiText(const char* s, float x, float y, float px, float r, float g, float b, float a);
    void uiTextCentered(const char* s, float cx, float y, float px, float r, float g, float b, float a);
    float textWidth(const char* s, float px) const;
    void uiEnd();

private:
    int mW = 1, mH = 1;
    float mScale = 1.0f;
    int mSceneW = 1, mSceneH = 1;

    GLuint mSceneProg = 0, mDepthProg = 0, mPostProg = 0, mUiProg = 0;
    GLuint mSceneFbo = 0, mSceneColor = 0, mSceneDepth = 0;
    GLuint mShadowFbo = 0, mShadowTex = 0;
    GLuint mAlbedoArray = 0, mNormalArray = 0;
    int mMaterialLayers = 0;
    GLuint mFontTex = 0;
    GLuint mUiVao = 0, mUiVbo = 0;
    GLuint mEmptyVao = 0;

    mat4 mLightVP;
    mat4 mViewProj;
    float mFrustum[6][4];      // world-space planes for chunk culling
    int mDrawCalls = 0, mTris = 0;

    // scene uniform locations
    GLint uMVP = -1, uModel = -1, uLightVP = -1, uCamPos = -1, uTint = -1;
    GLint uAlbedoArr = -1, uNormalArr = -1, uShadow = -1, uEmissive = -1;
    GLint uTorchPos = -1, uTorchDir = -1, uTorchColor = -1, uTorchParams = -1;
    GLint uNumLights = -1, uLightPosArr = -1, uLightColArr = -1, uLightRadArr = -1;
    GLint uAmbient = -1, uFogColor = -1, uFogDensity = -1;
    GLint dMVP = -1;
    GLint pTex = -1, pFear = -1, pTime = -1, pFade = -1, pDamage = -1, pAspect = -1;
    GLint uScreen = -1, uUiTex = -1;

    static const int SHADOW_SIZE = 1024;
    static const int UI_MAX_VERTS = 16384;
    struct UiVert { float x, y, u, v, r, g, b, a; };
    UiVert* mUiVerts = nullptr;
    int mUiCount = 0;

    void createTargets();
    void destroyTargets();
    void forgetGlState();
    bool buildMaterialArrays(const AssetPack* pack);
    void buildFont();
    void extractFrustum(const mat4& viewProj);
    void uiPushQuad(float x0, float y0, float x1, float y1,
                    float u0, float v0, float u1, float v1,
                    float r, float g, float b, float a);
    void uiPushTri(float x0, float y0, float x1, float y1, float x2, float y2,
                   float r, float g, float b, float a);
    void uiFlush();
};

} // namespace hm
