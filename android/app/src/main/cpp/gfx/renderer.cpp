#include "renderer.h"

#include <algorithm>

#include "../core/asset.h"
#include "shaders.h"

namespace hl {

bool Renderer::init(int width, int height) {
    width_ = width;
    height_ = height;

    const std::string skin = "#define SKINNED 1\n";
    const std::string tri = "#define TRIPLANAR 1\n";

    bool ok = true;
    ok &= scene_static_.build("scene", shaders::scene_vs, shaders::scene_fs, "");
    ok &= scene_skinned_.build("scene_skinned", shaders::scene_vs, shaders::scene_fs, skin);
    ok &= scene_tri_.build("scene_tri", shaders::scene_vs, shaders::scene_fs, tri);
    ok &= scene_tri_skinned_.build("scene_tri_skinned", shaders::scene_vs,
                                   shaders::scene_fs, skin + tri);
    ok &= shadow_static_.build("shadow", shaders::shadow_vs, shaders::shadow_fs, "");
    ok &= shadow_skinned_.build("shadow_skinned", shaders::shadow_vs, shaders::shadow_fs, skin);
    ok &= bright_.build("bright", shaders::fullscreen_vs, shaders::bright_fs, "");
    ok &= blur_.build("blur", shaders::fullscreen_vs, shaders::blur_fs, "");
    ok &= post_.build("post", shaders::fullscreen_vs, shaders::post_fs, "");
    if (!ok) {
        loge("shader build failed");
        return false;
    }

    if (!shadow_rt_.create(shadow_size_, shadow_size_, 0, true, true)) {
        loge("shadow map creation failed");
        return false;
    }

    build_targets();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    logi("renderer up: %dx%d, hdr=%d", width_, height_, (int)hdr_);
    return true;
}

void Renderer::build_targets() {
    rw_ = std::max(64, (int)(width_ * scale_));
    rh_ = std::max(64, (int)(height_ * scale_));

    hdr_rt_ = RenderTarget();
    bloom_a_ = RenderTarget();
    bloom_b_ = RenderTarget();

    // Half-float keeps bloom and the torch's hot core from clipping before the
    // tonemap. Not all GLES 3.0 drivers can render to it, so fall back quietly.
    hdr_ = hdr_rt_.create(rw_, rh_, GL_RGBA16F, true, false);
    if (!hdr_) {
        logi("RGBA16F not renderable, falling back to RGBA8");
        hdr_rt_ = RenderTarget();
        if (!hdr_rt_.create(rw_, rh_, GL_RGBA8, true, false)) {
            loge("scene target creation failed");
        }
    }

    int bw = std::max(32, rw_ / 2);
    int bh = std::max(32, rh_ / 2);
    GLenum bfmt = hdr_ ? GL_RGBA16F : GL_RGBA8;
    bloom_a_.create(bw, bh, bfmt, false, false);
    bloom_b_.create(bw, bh, bfmt, false, false);
}

void Renderer::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    build_targets();
}

void Renderer::set_render_scale(float s) {
    s = clampf(s, 0.5f, 1.0f);
    if (std::fabs(s - scale_) < 0.01f) return;
    scale_ = s;
    build_targets();
}

void Renderer::shutdown() {
    gfx_shutdown_fullscreen();
}

mat4 Renderer::torch_view_proj(const SceneView& v) const {
    // Widen the shadow frustum past the light cone so geometry just outside the
    // beam still casts into it — otherwise shadows pop at the cone edge.
    float half_angle = std::acos(clampf(v.torch_outer, -1.0f, 1.0f));
    float fov = clampf(half_angle * 2.4f, 0.35f, 2.6f);
    mat4 proj = mat4_perspective(fov, 1.0f, 0.12f, v.torch_range);

    v3 dir = normalize(v.torch_dir);
    v3 up = std::fabs(dir.y) > 0.95f ? v3(0, 0, 1) : v3(0, 1, 0);
    mat4 view = mat4_look_at(v.torch_pos, v.torch_pos + dir, up);
    return proj * view;
}

void Renderer::shadow_pass(const SceneView& v, const std::vector<DrawItem>& items) {
    light_vp_ = torch_view_proj(v);

    shadow_rt_.bind();
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    // Front-face culling in the shadow pass pushes peter-panning behind the
    // surface instead of detaching contact shadows from the floor.
    glCullFace(GL_FRONT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.6f, 3.0f);

    for (const auto& it : items) {
        if (!it.casts_shadow || !it.mesh) continue;
        Shader& sh = it.animator ? shadow_skinned_ : shadow_static_;
        sh.use();
        sh.set("uViewProj", light_vp_);
        sh.set("uModel", it.model);
        if (it.animator) {
            const auto& pal = it.animator->palette();
            if (!pal.empty()) {
                sh.set_array("uBones", pal.data(), std::min((int)pal.size(), 24));
            }
        }
        it.mesh->draw();
    }

    glDisable(GL_POLYGON_OFFSET_FILL);
    glCullFace(GL_BACK);
}

void Renderer::scene_pass(const SceneView& v, const std::vector<DrawItem>& items) {
    hdr_rt_.bind();
    glClearColor(v.fog_color.x, v.fog_color.y, v.fog_color.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    mat4 view_proj = v.proj * v.view;
    draw_calls_ = 0;
    triangles_ = 0;

    Shader* last = nullptr;
    for (const auto& it : items) {
        if (!it.mesh || !it.material) continue;
        const Material& m = *it.material;
        Shader* sh = m.triplanar
                         ? (it.animator ? &scene_tri_skinned_ : &scene_tri_)
                         : (it.animator ? &scene_skinned_ : &scene_static_);
        sh->use();

        if (sh != last) {
            // Per-frame uniforms only need resending when the program changes.
            sh->set("uViewProj", view_proj);
            sh->set("uLightViewProj", light_vp_);
            sh->set("uCamPos", v.cam_pos);
            sh->set("uTorchPos", v.torch_pos);
            sh->set("uTorchDir", normalize(v.torch_dir));
            sh->set("uTorchColor", v.torch_color * v.torch_intensity);
            sh->set("uTorchInner", v.torch_inner);
            sh->set("uTorchOuter", v.torch_outer);
            sh->set("uTorchRange", v.torch_range);
            sh->set("uAmbient", v.ambient);
            sh->set("uFogColor", v.fog_color);
            sh->set("uFogDensity", v.fog_density);
            sh->set("uShadowTexel", 1.0f / (float)shadow_size_);
            sh->set("uAlbedo", 0);
            sh->set("uNormalMap", 1);
            sh->set("uORM", 2);
            sh->set("uShadow", 3);

            int n = std::min((int)v.points.size(), 6);
            sh->set("uPointCount", n);
            for (int i = 0; i < n; i++) {
                char buf[32];
                snprintf(buf, sizeof(buf), "uPointPos[%d]", i);
                sh->set(buf, v4(v.points[i].pos, v.points[i].radius));
                snprintf(buf, sizeof(buf), "uPointColor[%d]", i);
                sh->set(buf, v4(v.points[i].color, v.points[i].intensity));
            }
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, shadow_rt_.depth());
            last = sh;
        }

        sh->set("uModel", it.model);
        sh->set("uUVScale", m.uv_scale);
        sh->set("uTint", v4(m.tint, m.emissive));
        sh->set("uWetness", m.wetness);
        if (it.animator) {
            const auto& pal = it.animator->palette();
            if (!pal.empty()) {
                sh->set_array("uBones", pal.data(), std::min((int)pal.size(), 24));
            }
        }

        if (m.albedo) m.albedo->bind(0);
        if (m.normal) m.normal->bind(1);
        if (m.orm) m.orm->bind(2);

        it.mesh->draw();
        draw_calls_++;
        triangles_ += it.mesh->triangles();
    }
}

void Renderer::bloom_pass() {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    bloom_a_.bind();
    bright_.use();
    bright_.set("uTex", 0);
    bright_.set("uThreshold", 1.05f);
    bright_.set("uKnee", 0.6f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdr_rt_.color());
    draw_fullscreen();

    // Two separable passes. More would be softer, but this is a phone.
    for (int i = 0; i < 2; i++) {
        bloom_b_.bind();
        blur_.use();
        blur_.set("uTex", 0);
        blur_.set("uDir", v2(1.0f / (float)bloom_a_.width(), 0.0f));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, bloom_a_.color());
        draw_fullscreen();

        bloom_a_.bind();
        blur_.set("uDir", v2(0.0f, 1.0f / (float)bloom_b_.height()));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, bloom_b_.color());
        draw_fullscreen();
    }
}

void Renderer::composite(const PostParams& p, float time) {
    glBindFramebuffer(GL_FRAMEBUFFER, output_fbo_);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    post_.use();
    post_.set("uScene", 0);
    post_.set("uBloom", 1);
    post_.set("uTime", time);
    post_.set("uExposure", p.exposure);
    post_.set("uBloomAmount", p.bloom);
    post_.set("uVignette", p.vignette);
    post_.set("uGrain", p.grain);
    post_.set("uChroma", p.chroma);
    post_.set("uDesat", p.desat);
    post_.set("uRedShift", p.red_shift);
    post_.set("uFlash", p.flash);
    post_.set("uBlind", p.blind);
    post_.set("uGlitch", p.glitch);
    post_.set("uScanline", p.scanline);
    post_.set("uResolution", v2((float)rw_, (float)rh_));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdr_rt_.color());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloom_a_.color());
    draw_fullscreen();

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::render(const SceneView& v, const std::vector<DrawItem>& items,
                      const PostParams& post, float time) {
    shadow_pass(v, items);
    scene_pass(v, items);
    bloom_pass();
    composite(post, time);
}

}  // namespace hl
