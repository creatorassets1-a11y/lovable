#include "gfx.h"

#include <cstring>
#include <algorithm>

#include "../core/asset.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../third_party/stb_image.h"

namespace hl {

const char* gl_error_string(GLenum e) {
    switch (e) {
        case GL_NO_ERROR: return "none";
        case GL_INVALID_ENUM: return "invalid enum";
        case GL_INVALID_VALUE: return "invalid value";
        case GL_INVALID_OPERATION: return "invalid operation";
        case GL_OUT_OF_MEMORY: return "out of memory";
        case GL_INVALID_FRAMEBUFFER_OPERATION: return "invalid framebuffer op";
        default: return "unknown";
    }
}

bool gl_check(const char* where) {
    GLenum e = glGetError();
    if (e == GL_NO_ERROR) return true;
    loge("GL error at %s: %s (0x%x)", where, gl_error_string(e), e);
    return false;
}

// ==================================================================== shader

Shader::~Shader() {
    if (prog_) glDeleteProgram(prog_);
}

static GLuint compile_stage(GLenum type, const std::string& src, const char* name) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        GLsizei n = 0;
        glGetShaderInfoLog(s, sizeof(log) - 1, &n, log);
        log[n] = 0;
        loge("%s %s shader failed:\n%s", name,
             type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool Shader::build(const char* name, const std::string& vs, const std::string& fs,
                   const std::string& defines) {
    name_ = name;
    const std::string header = "#version 300 es\n" + defines;
    GLuint v = compile_stage(GL_VERTEX_SHADER, header + vs, name);
    if (!v) return false;
    GLuint f = compile_stage(GL_FRAGMENT_SHADER, header + fs, name);
    if (!f) { glDeleteShader(v); return false; }

    prog_ = glCreateProgram();
    glAttachShader(prog_, v);
    glAttachShader(prog_, f);
    glLinkProgram(prog_);
    glDeleteShader(v);
    glDeleteShader(f);

    GLint ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        GLsizei n = 0;
        glGetProgramInfoLog(prog_, sizeof(log) - 1, &n, log);
        log[n] = 0;
        loge("%s link failed:\n%s", name, log);
        glDeleteProgram(prog_);
        prog_ = 0;
        return false;
    }
    return true;
}

void Shader::use() const { glUseProgram(prog_); }

GLint Shader::loc(const char* n) {
    auto it = cache_.find(n);
    if (it != cache_.end()) return it->second;
    GLint l = glGetUniformLocation(prog_, n);
    cache_[n] = l;
    return l;
}

void Shader::set(const char* n, int v) { glUniform1i(loc(n), v); }
void Shader::set(const char* n, float v) { glUniform1f(loc(n), v); }
void Shader::set(const char* n, v2 v) { glUniform2f(loc(n), v.x, v.y); }
void Shader::set(const char* n, v3 v) { glUniform3f(loc(n), v.x, v.y, v.z); }
void Shader::set(const char* n, v4 v) { glUniform4f(loc(n), v.x, v.y, v.z, v.w); }
void Shader::set(const char* n, const mat4& v) {
    glUniformMatrix4fv(loc(n), 1, GL_FALSE, v.m);
}
void Shader::set_array(const char* n, const mat4* v, int count) {
    GLint l = loc(n);
    if (l >= 0 && count > 0) glUniformMatrix4fv(l, count, GL_FALSE, v[0].m);
}

// =================================================================== texture

Texture::~Texture() {
    if (tex_) glDeleteTextures(1, &tex_);
}

bool Texture::load(const std::string& path, bool srgb, bool repeat) {
    auto bytes = asset_read(path);
    if (bytes.empty()) return make_solid(srgb ? 0xFF808080u : 0xFF8080FFu);

    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
    if (!px) {
        loge("decode failed: %s", path.c_str());
        return make_solid(srgb ? 0xFF808080u : 0xFF8080FFu);
    }
    w_ = w; h_ = h;

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    // sRGB for anything the eye reads as colour; linear for normals and masks.
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);

    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GLenum wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

    // Anisotropy matters a lot for floors seen at grazing angles.
    GLfloat maxAniso = 0;
    glGetFloatv(0x84FF /* GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT */, &maxAniso);
    if (maxAniso > 1.0f) {
        glTexParameterf(GL_TEXTURE_2D, 0x84FE /* TEXTURE_MAX_ANISOTROPY_EXT */,
                        maxAniso > 8.0f ? 8.0f : maxAniso);
    }
    glGetError();  // anisotropy is optional; do not leak its error
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool Texture::make_solid(uint32_t rgba) {
    if (!tex_) glGenTextures(1, &tex_);
    w_ = h_ = 1;
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Texture::bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex_);
}

// ====================================================================== mesh
//
// .hmsh layout, all little-endian:
//   'HMS1' | flags u32 | vertexCount u32 | indexCount u32 | boneCount u32 |
//   animCount u32 | bboxMin 3f | bboxMax 3f
//   vertices: pos 3f, normal 3f, tangent 3f, sign 1f, uv 2f   (48 bytes)
//   if skinned: boneIndex 4xu8 [all verts], then boneWeight 4xu8 [all verts]
//   indices: u32 * indexCount
//   bones:   name[32], parent i32, inverseBind 16f
//   anims:   name[32], duration f32, frames u32, fps f32, loop u32,
//            rot 4f * frames * bones, pos 3f * frames * bones

namespace {
struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;
    template <typename T>
    T get() {
        if (p + sizeof(T) > end) { ok = false; return T{}; }
        T v;
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        return v;
    }
    bool take(void* dst, size_t n) {
        if (p + n > end) { ok = false; return false; }
        std::memcpy(dst, p, n);
        p += n;
        return true;
    }
};
constexpr int VERT_FLOATS = 12;
}  // namespace

Mesh::~Mesh() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (skin_vbo_) glDeleteBuffers(1, &skin_vbo_);
}

bool Mesh::load(const std::string& path) {
    auto data = asset_read(path);
    if (data.size() < 44) { loge("mesh too small: %s", path.c_str()); return false; }

    Reader r{data.data(), data.data() + data.size()};
    char magic[4];
    r.take(magic, 4);
    if (std::memcmp(magic, "HMS1", 4) != 0) {
        loge("bad mesh magic: %s", path.c_str());
        return false;
    }

    uint32_t flags = r.get<uint32_t>();
    uint32_t vcount = r.get<uint32_t>();
    uint32_t icount = r.get<uint32_t>();
    uint32_t bcount = r.get<uint32_t>();
    uint32_t acount = r.get<uint32_t>();
    bmin_ = {r.get<float>(), r.get<float>(), r.get<float>()};
    bmax_ = {r.get<float>(), r.get<float>(), r.get<float>()};
    skinned_ = (flags & 1u) != 0;
    index_count_ = (int)icount;

    std::vector<float> verts(vcount * VERT_FLOATS);
    if (!r.take(verts.data(), verts.size() * sizeof(float))) {
        loge("mesh truncated (verts): %s", path.c_str());
        return false;
    }

    std::vector<uint8_t> bidx, bw;
    if (skinned_) {
        bidx.resize(vcount * 4);
        bw.resize(vcount * 4);
        r.take(bidx.data(), bidx.size());
        r.take(bw.data(), bw.size());
    }

    std::vector<uint32_t> indices(icount);
    if (!r.take(indices.data(), indices.size() * sizeof(uint32_t))) {
        loge("mesh truncated (indices): %s", path.c_str());
        return false;
    }

    bones_.resize(bcount);
    for (uint32_t i = 0; i < bcount; i++) {
        r.take(bones_[i].name, 32);
        bones_[i].name[31] = 0;
        bones_[i].parent = r.get<int32_t>();
        r.take(bones_[i].inverse_bind.m, 16 * sizeof(float));
    }

    clips_.resize(acount);
    for (uint32_t i = 0; i < acount; i++) {
        AnimClip& c = clips_[i];
        r.take(c.name, 32);
        c.name[31] = 0;
        c.duration = r.get<float>();
        c.frames = (int)r.get<uint32_t>();
        c.fps = r.get<float>();
        c.loop = r.get<uint32_t>() != 0;
        size_t n = (size_t)c.frames * bcount;
        c.rot.resize(n);
        c.pos.resize(n);
        r.take(c.rot.data(), n * sizeof(quat));
        r.take(c.pos.data(), n * sizeof(v3));
    }

    if (!r.ok) { loge("mesh truncated: %s", path.c_str()); return false; }

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);

    const GLsizei stride = VERT_FLOATS * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)(10 * sizeof(float)));

    if (skinned_) {
        std::vector<uint8_t> interleaved(vcount * 8);
        for (uint32_t i = 0; i < vcount; i++) {
            std::memcpy(&interleaved[i * 8], &bidx[i * 4], 4);
            std::memcpy(&interleaved[i * 8 + 4], &bw[i * 4], 4);
        }
        glGenBuffers(1, &skin_vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, skin_vbo_);
        glBufferData(GL_ARRAY_BUFFER, interleaved.size(), interleaved.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(4);
        glVertexAttribIPointer(4, 4, GL_UNSIGNED_BYTE, 8, (void*)0);
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, 8, (void*)4);
    }

    glGenBuffers(1, &ebo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t),
                 indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
    logi("mesh %s: %u verts, %d tris, %u bones, %u clips", path.c_str(), vcount,
         index_count_ / 3, bcount, acount);
    return gl_check("mesh upload");
}

bool Mesh::build(const std::vector<float>& verts, const std::vector<uint32_t>& indices) {
    if (verts.empty() || indices.empty()) return false;
    skinned_ = false;
    index_count_ = (int)indices.size();

    bmin_ = v3(1e9f, 1e9f, 1e9f);
    bmax_ = v3(-1e9f, -1e9f, -1e9f);
    for (size_t i = 0; i + VERT_FLOATS <= verts.size(); i += VERT_FLOATS) {
        bmin_.x = std::min(bmin_.x, verts[i]);     bmax_.x = std::max(bmax_.x, verts[i]);
        bmin_.y = std::min(bmin_.y, verts[i + 1]); bmax_.y = std::max(bmax_.y, verts[i + 1]);
        bmin_.z = std::min(bmin_.z, verts[i + 2]); bmax_.z = std::max(bmax_.z, verts[i + 2]);
    }

    if (!vao_) glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    if (!vbo_) glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);

    const GLsizei stride = VERT_FLOATS * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)(10 * sizeof(float)));

    if (!ebo_) glGenBuffers(1, &ebo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t),
                 indices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    return gl_check("mesh build");
}

void Mesh::draw() const {
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
}

// ================================================================= animator

void Animator::init(const std::vector<Bone>* bones, const std::vector<AnimClip>* clips) {
    bones_ = bones;
    clips_ = clips;
    palette_.assign(bones->size(), mat4_identity());
    model_.assign(bones->size(), mat4_identity());
    ra_.resize(bones->size());
    rb_.resize(bones->size());
    pa_.resize(bones->size());
    pb_.resize(bones->size());
}

int Animator::find(const std::string& name) const {
    if (!clips_) return -1;
    for (size_t i = 0; i < clips_->size(); i++) {
        if (name == (*clips_)[i].name) return (int)i;
    }
    return -1;
}

void Animator::play(const std::string& name, float blend, bool restart) {
    int idx = find(name);
    if (idx < 0 || (idx == cur_ && !restart)) return;
    prev_ = cur_;
    prev_time_ = cur_time_;
    cur_ = idx;
    cur_time_ = 0;
    blend_dur_ = (prev_ >= 0) ? blend : 0.0f;
    blend_ = blend_dur_;
    current_name_ = name;
}

float Animator::phase() const {
    if (cur_ < 0) return 0;
    const AnimClip& c = (*clips_)[cur_];
    return c.duration > 0 ? clampf(cur_time_ / c.duration, 0, 1) : 1;
}

bool Animator::finished() const {
    if (cur_ < 0) return true;
    const AnimClip& c = (*clips_)[cur_];
    return !c.loop && cur_time_ >= c.duration;
}

void Animator::sample(int ci, float time, std::vector<quat>& r, std::vector<v3>& p) const {
    const AnimClip& c = (*clips_)[ci];
    const int nb = (int)bones_->size();
    if (c.frames <= 0) return;

    float t = time;
    if (c.loop && c.duration > 0) t = std::fmod(t, c.duration);
    t = clampf(t, 0.0f, c.duration);

    float ft = t * c.fps;
    int f0 = (int)ft;
    float frac = ft - (float)f0;
    int f1 = f0 + 1;
    if (f0 >= c.frames - 1) {
        if (c.loop) { f0 = c.frames - 1; f1 = 0; }
        else { f0 = f1 = c.frames - 1; frac = 0; }
    }

    for (int b = 0; b < nb; b++) {
        r[b] = slerp(c.rot[(size_t)f0 * nb + b], c.rot[(size_t)f1 * nb + b], frac);
        p[b] = lerp(c.pos[(size_t)f0 * nb + b], c.pos[(size_t)f1 * nb + b], frac);
    }
}

void Animator::update(float dt) {
    if (!bones_ || cur_ < 0) return;
    const int nb = (int)bones_->size();

    cur_time_ += dt * speed_;
    if (prev_ >= 0) {
        prev_time_ += dt * speed_;
        blend_ -= dt;
        if (blend_ <= 0) prev_ = -1;
    }

    sample(cur_, cur_time_, ra_, pa_);
    if (prev_ >= 0) {
        sample(prev_, prev_time_, rb_, pb_);
        // blend_ counts down from blend_dur_, so w goes 1 -> 0 on the old clip.
        float w = blend_dur_ > 0 ? clampf(blend_ / blend_dur_, 0, 1) : 0;
        for (int b = 0; b < nb; b++) {
            ra_[b] = slerp(ra_[b], rb_[b], w);
            pa_[b] = lerp(pa_[b], pb_[b], w);
        }
    }

    // Bones are stored parents-first, so one forward pass composes the hierarchy.
    for (int b = 0; b < nb; b++) {
        mat4 local = mat4_trs(pa_[b], ra_[b], v3(1, 1, 1));
        int par = (*bones_)[b].parent;
        model_[b] = (par >= 0) ? model_[par] * local : local;
        palette_[b] = model_[b] * (*bones_)[b].inverse_bind;
    }
}

bool Animator::bone_matrix(const std::string& name, mat4* out) const {
    if (!bones_) return false;
    for (size_t i = 0; i < bones_->size(); i++) {
        if (name == (*bones_)[i].name) { *out = model_[i]; return true; }
    }
    return false;
}

// ============================================================== rendertarget

RenderTarget::~RenderTarget() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (color_) glDeleteTextures(1, &color_);
    if (depth_tex_) glDeleteTextures(1, &depth_tex_);
    if (depth_rb_) glDeleteRenderbuffers(1, &depth_rb_);
}

bool RenderTarget::create(int w, int h, GLenum color_fmt, bool depth, bool depth_texture) {
    w_ = w; h_ = h;
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    if (color_fmt) {
        glGenTextures(1, &color_);
        glBindTexture(GL_TEXTURE_2D, color_);
        glTexStorage2D(GL_TEXTURE_2D, 1, color_fmt, w, h);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
    } else {
        glDrawBuffers(0, nullptr);
    }

    if (depth) {
        if (depth_texture) {
            glGenTextures(1, &depth_tex_);
            glBindTexture(GL_TEXTURE_2D, depth_tex_);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT24, w, h);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            // Hardware PCF: sampler2DShadow gives free 2x2 filtering.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_tex_, 0);
        } else {
            glGenRenderbuffers(1, &depth_rb_);
            glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                      GL_RENDERBUFFER, depth_rb_);
        }
    }

    if (!color_fmt) {
        GLenum none = GL_NONE;
        glDrawBuffers(1, &none);
        glReadBuffer(GL_NONE);
    }

    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        loge("framebuffer incomplete: 0x%x (%dx%d)", st, w, h);
        return false;
    }
    return true;
}

void RenderTarget::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w_, h_);
}

// ============================================================== fullscreen

namespace {
GLuint g_fs_vao = 0;
}

void draw_fullscreen() {
    if (!g_fs_vao) glGenVertexArrays(1, &g_fs_vao);
    glBindVertexArray(g_fs_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void gfx_shutdown_fullscreen() {
    if (g_fs_vao) { glDeleteVertexArrays(1, &g_fs_vao); g_fs_vao = 0; }
}

}  // namespace hl
