#pragma once
#include <vector>

#include "gfx.h"

namespace hl {

struct Material {
    Texture* albedo = nullptr;
    Texture* normal = nullptr;
    Texture* orm = nullptr;
    v2 uv_scale{1, 1};
    v3 tint{1, 1, 1};
    float emissive = 0.0f;
    float wetness = 0.0f;
    bool triplanar = false;
};

struct DrawItem {
    const Mesh* mesh = nullptr;
    mat4 model;
    const Material* material = nullptr;
    const Animator* animator = nullptr;   // null for static geometry
    bool casts_shadow = true;
};

struct PointLight {
    v3 pos;
    float radius = 4.0f;
    v3 color{1, 0.8f, 0.6f};
    float intensity = 1.0f;
};

struct SceneView {
    mat4 view, proj;
    v3 cam_pos;

    v3 torch_pos, torch_dir;
    v3 torch_color{1.0f, 0.92f, 0.78f};
    float torch_intensity = 26.0f;
    float torch_inner = 0.90f;      // cos of inner cone half-angle (~26 deg)
    float torch_outer = 0.72f;      // ~44 deg
    float torch_range = 22.0f;

    v3 ambient{0.020f, 0.022f, 0.030f};
    v3 fog_color{0.020f, 0.021f, 0.026f};
    float fog_density = 0.055f;

    std::vector<PointLight> points;
};

struct PostParams {
    float exposure = 1.0f;
    float bloom = 0.55f;
    float vignette = 0.35f;
    float grain = 0.055f;
    float chroma = 0.0f;
    float desat = 0.0f;
    float red_shift = 0.0f;
    float flash = 0.0f;
    float blind = 0.0f;
    float glitch = 0.0f;
    float scanline = 0.012f;
};

class Renderer {
public:
    bool init(int width, int height);
    void resize(int width, int height);
    void shutdown();

    void render(const SceneView& view, const std::vector<DrawItem>& items,
                const PostParams& post, float time);

    /** Where the final composite lands. 0 (the default) is the window on
        Android; the offscreen preview harness points this at its own FBO,
        because a surfaceless context has no default framebuffer at all. */
    void set_output_framebuffer(GLuint fbo) { output_fbo_ = fbo; }

    /** Scale the internal render buffer (0.5 .. 1.0) to trade sharpness for fps. */
    void set_render_scale(float s);
    float render_scale() const { return scale_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int last_draw_calls() const { return draw_calls_; }
    int last_triangles() const { return triangles_; }

private:
    void build_targets();
    void shadow_pass(const SceneView& v, const std::vector<DrawItem>& items);
    void scene_pass(const SceneView& v, const std::vector<DrawItem>& items);
    void bloom_pass();
    void composite(const PostParams& p, float time);
    mat4 torch_view_proj(const SceneView& v) const;

    int width_ = 0, height_ = 0;
    int rw_ = 0, rh_ = 0;
    float scale_ = 1.0f;
    int shadow_size_ = 1024;
    bool hdr_ = true;

    Shader scene_static_, scene_skinned_, scene_tri_, scene_tri_skinned_;
    Shader shadow_static_, shadow_skinned_;
    Shader bright_, blur_, post_;

    RenderTarget hdr_rt_, shadow_rt_, bloom_a_, bloom_b_;
    mat4 light_vp_;
    int draw_calls_ = 0, triangles_ = 0;
    GLuint output_fbo_ = 0;
};

}  // namespace hl
