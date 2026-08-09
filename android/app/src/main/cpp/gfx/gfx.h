// GPU resources: shaders, textures, meshes, skeletal animation.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../core/hmath.h"

#ifdef __ANDROID__
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#else
#include <GLES3/gl3.h>
#endif

namespace hl {

using namespace hm;

// ------------------------------------------------------------------ shader

class Shader {
public:
    ~Shader();
    /** Compiles from source with `#version 300 es` prepended. */
    bool build(const char* name, const std::string& vs, const std::string& fs,
               const std::string& defines = "");
    void use() const;
    GLuint id() const { return prog_; }

    void set(const char* n, int v);
    void set(const char* n, float v);
    void set(const char* n, v2 v);
    void set(const char* n, v3 v);
    void set(const char* n, v4 v);
    void set(const char* n, const mat4& v);
    void set_array(const char* n, const mat4* v, int count);

private:
    GLint loc(const char* n);
    GLuint prog_ = 0;
    std::map<std::string, GLint> cache_;
    std::string name_;
};

// ----------------------------------------------------------------- texture

class Texture {
public:
    ~Texture();
    /** PNG via stb_image, mipmapped, anisotropic where available. */
    bool load(const std::string& path, bool srgb, bool repeat = true);
    /** 1x1 fallback so a missing map never breaks a draw. */
    bool make_solid(uint32_t rgba);
    void bind(int unit) const;
    GLuint id() const { return tex_; }
    int width() const { return w_; }
    int height() const { return h_; }

private:
    GLuint tex_ = 0;
    int w_ = 0, h_ = 0;
};

// -------------------------------------------------------------- animation

struct Bone {
    char name[32] = {};
    int parent = -1;
    mat4 inverse_bind;
};

struct AnimClip {
    char name[32] = {};
    float duration = 0;
    int frames = 0;
    float fps = 30;
    bool loop = true;
    std::vector<quat> rot;   // frames * boneCount
    std::vector<v3> pos;     // frames * boneCount
};

/** Samples a clip and produces the skinning palette for a draw. */
class Animator {
public:
    void init(const std::vector<Bone>* bones, const std::vector<AnimClip>* clips);
    /** Cross-fades to `clip` over `blend` seconds. Ignored if already playing it. */
    void play(const std::string& clip, float blend = 0.25f, bool restart = false);
    void update(float dt);
    const std::vector<mat4>& palette() const { return palette_; }
    const std::string& current() const { return current_name_; }
    /** Normalised progress through the active clip, for animation-driven events. */
    float phase() const;
    bool finished() const;
    /** Model-space transform of a named bone, for attaching things to hands. */
    bool bone_matrix(const std::string& name, mat4* out) const;

private:
    void sample(int clip, float time, std::vector<quat>& r, std::vector<v3>& p) const;
    int find(const std::string& name) const;

    const std::vector<Bone>* bones_ = nullptr;
    const std::vector<AnimClip>* clips_ = nullptr;
    int cur_ = -1, prev_ = -1;
    float cur_time_ = 0, prev_time_ = 0;
    float blend_ = 0, blend_dur_ = 0;
    float speed_ = 1;
    std::string current_name_;
    std::vector<mat4> palette_;
    std::vector<mat4> model_;
    std::vector<quat> ra_, rb_;
    std::vector<v3> pa_, pb_;

public:
    void set_speed(float s) { speed_ = s; }
    float speed() const { return speed_; }
};

// -------------------------------------------------------------------- mesh

class Mesh {
public:
    ~Mesh();
    bool load(const std::string& path);
    /** Upload procedurally built geometry. Same 12-float layout as .hmsh:
        pos 3, normal 3, tangent 3, sign 1, uv 2. */
    bool build(const std::vector<float>& verts, const std::vector<uint32_t>& indices);
    void draw() const;
    bool skinned() const { return skinned_; }
    int bone_count() const { return (int)bones_.size(); }
    const std::vector<Bone>& bones() const { return bones_; }
    const std::vector<AnimClip>& clips() const { return clips_; }
    v3 bbox_min() const { return bmin_; }
    v3 bbox_max() const { return bmax_; }
    int triangles() const { return index_count_ / 3; }

private:
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0, skin_vbo_ = 0;
    int index_count_ = 0;
    bool skinned_ = false;
    v3 bmin_, bmax_;
    std::vector<Bone> bones_;
    std::vector<AnimClip> clips_;
};

// ------------------------------------------------------------ framebuffer

/** Colour+depth render target. `color_fmt` 0 means depth-only (shadow map). */
class RenderTarget {
public:
    ~RenderTarget();
    bool create(int w, int h, GLenum color_fmt, bool depth, bool depth_texture = false);
    void bind() const;
    int width() const { return w_; }
    int height() const { return h_; }
    GLuint color() const { return color_; }
    GLuint depth() const { return depth_tex_; }

private:
    GLuint fbo_ = 0, color_ = 0, depth_rb_ = 0, depth_tex_ = 0;
    int w_ = 0, h_ = 0;
};

/** Full-screen triangle. Cheaper than a quad and avoids the diagonal seam. */
void draw_fullscreen();
void gfx_shutdown_fullscreen();

const char* gl_error_string(GLenum e);
bool gl_check(const char* where);

}  // namespace hl
