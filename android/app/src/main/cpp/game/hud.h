// Immediate-mode 2D overlay: text, bars, touch controls, fades.
//
// Everything is batched into one dynamic buffer and flushed in at most two draw
// calls (untextured, then font). Layout is expressed in "design units" scaled to
// the short edge of the screen, so nothing can overflow a small phone.
#pragma once
#include <string>
#include <vector>

#include "../gfx/gfx.h"

namespace hl {

using namespace hm;

struct Glyph {
    short x, y, w, h;
    short bearing_x, bearing_y;
    float advance;
};

class Font {
public:
    bool load(const std::string& png, const std::string& bin);
    /** Width of `text` at the given pixel height. */
    float measure(const std::string& text, float px) const;
    float line_height(float px) const { return line_ * px / (float)size_; }
    const Glyph* glyph(char c) const;
    float scale(float px) const { return px / (float)size_; }
    Texture& texture() { return tex_; }
    int atlas() const { return atlas_; }

private:
    Texture tex_;
    std::vector<Glyph> glyphs_;
    int atlas_ = 0, size_ = 0, line_ = 0, first_ = 32;
};

enum class Align { Left, Center, Right };

class Hud {
public:
    bool init();
    void begin(int width, int height);
    void end();

    /** One hundredth of the screen's short edge. Every size derives from this. */
    float u() const { return u_; }
    float width() const { return (float)width_; }
    float height() const { return (float)height_; }
    /** Safe-area insets in pixels, set by the platform layer. */
    void set_insets(float l, float t, float r, float b);
    float inset_l() const { return inset_l_; }
    float inset_r() const { return inset_r_; }
    float inset_t() const { return inset_t_; }
    float inset_b() const { return inset_b_; }

    void rect(float x, float y, float w, float h, v4 color);
    void rect_outline(float x, float y, float w, float h, float thick, v4 color);
    void circle(float cx, float cy, float r, v4 color, int segments = 24);
    void ring(float cx, float cy, float r, float thick, v4 color, int segments = 28);
    void text(const std::string& s, float x, float y, float px, v4 color,
              Align align = Align::Left, bool title = false);
    /** Word-wrapped block. Returns the height it occupied. */
    float text_block(const std::string& s, float cx, float y, float max_w,
                     float px, v4 color, bool title = false);
    void fade(v4 color);

private:
    void flush();
    void push_quad(float x, float y, float w, float h, v4 c,
                   float u0, float v0, float u1, float v1, bool textured);

    Shader shader_;
    Font font_, title_;
    GLuint vao_ = 0, vbo_ = 0;
    std::vector<float> verts_;
    bool batch_textured_ = false;
    Texture* batch_tex_ = nullptr;
    Texture white_;
    int width_ = 0, height_ = 0;
    float u_ = 1;
    float inset_l_ = 0, inset_t_ = 0, inset_r_ = 0, inset_b_ = 0;
};

}  // namespace hl
