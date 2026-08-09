#include "hud.h"

#include <cstring>

#include "../core/asset.h"
#include "../gfx/shaders.h"

namespace hl {

// ==================================================================== font

bool Font::load(const std::string& png, const std::string& bin) {
    if (!tex_.load(png, false, false)) return false;
    auto data = asset_read(bin);
    if (data.size() < 24 || std::memcmp(data.data(), "HFNT", 4) != 0) {
        loge("bad font metrics: %s", bin.c_str());
        return false;
    }
    const uint8_t* p = data.data() + 4;
    auto u32 = [&p]() { uint32_t v; std::memcpy(&v, p, 4); p += 4; return v; };
    atlas_ = (int)u32();
    size_ = (int)u32();
    line_ = (int)u32();
    first_ = (int)u32();
    uint32_t count = u32();

    glyphs_.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        if (p + 16 > data.data() + data.size()) break;
        Glyph g{};
        std::memcpy(&g.x, p, 2); p += 2;
        std::memcpy(&g.y, p, 2); p += 2;
        std::memcpy(&g.w, p, 2); p += 2;
        std::memcpy(&g.h, p, 2); p += 2;
        std::memcpy(&g.bearing_x, p, 2); p += 2;
        std::memcpy(&g.bearing_y, p, 2); p += 2;
        std::memcpy(&g.advance, p, 4); p += 4;
        glyphs_[i] = g;
    }
    return true;
}

const Glyph* Font::glyph(char c) const {
    int i = (int)(unsigned char)c - first_;
    if (i < 0 || i >= (int)glyphs_.size()) return nullptr;
    return &glyphs_[i];
}

float Font::measure(const std::string& s, float px) const {
    float sc = scale(px), w = 0;
    for (char c : s) {
        const Glyph* g = glyph(c);
        if (g) w += g->advance * sc;
    }
    return w;
}

// ===================================================================== hud

bool Hud::init() {
    if (!shader_.build("ui", shaders::ui_vs, shaders::ui_fs, "")) return false;
    white_.make_solid(0xFFFFFFFFu);
    if (!font_.load("tex/font.png", "tex/font.bin")) loge("hud font missing");
    if (!title_.load("tex/font_title.png", "tex/font_title.bin")) loge("title font missing");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));
    glBindVertexArray(0);
    return true;
}

void Hud::set_insets(float l, float t, float r, float b) {
    inset_l_ = l; inset_t_ = t; inset_r_ = r; inset_b_ = b;
}

void Hud::begin(int width, int height) {
    width_ = width;
    height_ = height;
    u_ = (float)(width < height ? width : height) / 100.0f;
    verts_.clear();
    batch_textured_ = false;
    batch_tex_ = &white_;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    shader_.use();
    shader_.set("uScreen", v2((float)width_, (float)height_));
    shader_.set("uTex", 0);
}

void Hud::end() {
    flush();
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Hud::flush() {
    if (verts_.empty()) return;
    shader_.use();
    shader_.set("uTextured", batch_textured_ ? 1.0f : 0.0f);
    if (batch_tex_) batch_tex_->bind(0);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts_.size() * sizeof(float), verts_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(verts_.size() / 8));
    verts_.clear();
}

void Hud::push_quad(float x, float y, float w, float h, v4 c,
                    float u0, float v0, float u1, float v1, bool textured) {
    // Switching between textured and untextured means a new draw call.
    if (textured != batch_textured_) {
        flush();
        batch_textured_ = textured;
    }
    const float px[6] = {x, x + w, x + w, x, x + w, x};
    const float py[6] = {y, y, y + h, y, y + h, y + h};
    const float tu[6] = {u0, u1, u1, u0, u1, u0};
    const float tv[6] = {v0, v0, v1, v0, v1, v1};
    for (int i = 0; i < 6; i++) {
        verts_.push_back(px[i]);
        verts_.push_back(py[i]);
        verts_.push_back(tu[i]);
        verts_.push_back(tv[i]);
        verts_.push_back(c.x);
        verts_.push_back(c.y);
        verts_.push_back(c.z);
        verts_.push_back(c.w);
    }
}

void Hud::rect(float x, float y, float w, float h, v4 color) {
    if (batch_tex_ != &white_) { flush(); batch_tex_ = &white_; }
    push_quad(x, y, w, h, color, 0, 0, 1, 1, false);
}

void Hud::rect_outline(float x, float y, float w, float h, float t, v4 c) {
    rect(x, y, w, t, c);
    rect(x, y + h - t, w, t, c);
    rect(x, y + t, t, h - t * 2, c);
    rect(x + w - t, y + t, t, h - t * 2, c);
}

void Hud::circle(float cx, float cy, float r, v4 color, int segments) {
    if (batch_tex_ != &white_) { flush(); batch_tex_ = &white_; }
    if (batch_textured_) { flush(); batch_textured_ = false; }
    for (int i = 0; i < segments; i++) {
        float a0 = (float)i / segments * TAU;
        float a1 = (float)(i + 1) / segments * TAU;
        const float xs[3] = {cx, cx + std::cos(a0) * r, cx + std::cos(a1) * r};
        const float ys[3] = {cy, cy + std::sin(a0) * r, cy + std::sin(a1) * r};
        for (int k = 0; k < 3; k++) {
            verts_.push_back(xs[k]);
            verts_.push_back(ys[k]);
            verts_.push_back(0);
            verts_.push_back(0);
            verts_.push_back(color.x);
            verts_.push_back(color.y);
            verts_.push_back(color.z);
            verts_.push_back(color.w);
        }
    }
}

void Hud::ring(float cx, float cy, float r, float thick, v4 color, int segments) {
    if (batch_tex_ != &white_) { flush(); batch_tex_ = &white_; }
    if (batch_textured_) { flush(); batch_textured_ = false; }
    float ri = r - thick;
    for (int i = 0; i < segments; i++) {
        float a0 = (float)i / segments * TAU;
        float a1 = (float)(i + 1) / segments * TAU;
        float c0 = std::cos(a0), s0 = std::sin(a0);
        float c1 = std::cos(a1), s1 = std::sin(a1);
        const float xs[6] = {cx + c0 * ri, cx + c0 * r, cx + c1 * r,
                             cx + c0 * ri, cx + c1 * r, cx + c1 * ri};
        const float ys[6] = {cy + s0 * ri, cy + s0 * r, cy + s1 * r,
                             cy + s0 * ri, cy + s1 * r, cy + s1 * ri};
        for (int k = 0; k < 6; k++) {
            verts_.push_back(xs[k]);
            verts_.push_back(ys[k]);
            verts_.push_back(0);
            verts_.push_back(0);
            verts_.push_back(color.x);
            verts_.push_back(color.y);
            verts_.push_back(color.z);
            verts_.push_back(color.w);
        }
    }
}

void Hud::text(const std::string& s, float x, float y, float px, v4 color,
               Align align, bool title) {
    Font& f = title ? title_ : font_;
    if (batch_tex_ != &f.texture()) { flush(); batch_tex_ = &f.texture(); }

    float sc = f.scale(px);
    float w = f.measure(s, px);
    float cx = align == Align::Center ? x - w * 0.5f
             : align == Align::Right ? x - w : x;

    for (char c : s) {
        const Glyph* g = f.glyph(c);
        if (!g) continue;
        if (g->w > 1 && g->h > 1) {
            float gx = cx + g->bearing_x * sc;
            float gy = y + g->bearing_y * sc;
            float gw = g->w * sc, gh = g->h * sc;
            float inv = 1.0f / (float)f.atlas();
            push_quad(gx, gy, gw, gh, color,
                      g->x * inv, g->y * inv,
                      (g->x + g->w) * inv, (g->y + g->h) * inv, true);
        }
        cx += g->advance * sc;
    }
}

float Hud::text_block(const std::string& s, float cx, float y, float max_w,
                      float px, v4 color, bool title) {
    Font& f = title ? title_ : font_;
    std::vector<std::string> lines;
    std::string line, word;
    auto commit_word = [&]() {
        if (word.empty()) return;
        std::string candidate = line.empty() ? word : line + " " + word;
        if (f.measure(candidate, px) > max_w && !line.empty()) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
    };
    for (char c : s) {
        if (c == '\n') {
            commit_word();
            lines.push_back(line);
            line.clear();
        } else if (c == ' ') {
            commit_word();
        } else {
            word.push_back(c);
        }
    }
    commit_word();
    if (!line.empty()) lines.push_back(line);

    float lh = f.line_height(px) * 1.12f;
    for (size_t i = 0; i < lines.size(); i++) {
        text(lines[i], cx, y + i * lh, px, color, Align::Center, title);
    }
    return lines.size() * lh;
}

void Hud::fade(v4 color) {
    if (color.w <= 0.001f) return;
    rect(0, 0, (float)width_, (float)height_, color);
}

}  // namespace hl
