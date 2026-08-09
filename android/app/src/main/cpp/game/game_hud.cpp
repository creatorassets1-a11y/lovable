// The overlay: touch controls, meters, subtitles, and every full-screen card.
//
// Layout rule, enforced everywhere below: positions are built from hud.u() (one
// hundredth of the screen's short edge) and offset by the safe-area insets, and
// every text block is given an explicit max width to wrap inside. Nothing is
// positioned in raw pixels, so nothing can fall off a small screen or slide
// under a notch.

#include "game.h"
#include "story.h"

namespace hl {

namespace {
const v4 INK{0.86f, 0.83f, 0.78f, 1.0f};
const v4 DIM{0.55f, 0.52f, 0.48f, 1.0f};
const v4 BLOOD{0.78f, 0.22f, 0.16f, 1.0f};
const v4 SHADOW{0.0f, 0.0f, 0.0f, 0.62f};
}  // namespace

void Game::draw_hud(float time) {
    hud_.begin(width_, height_);
    const float u = hud_.u();
    const float W = hud_.width();
    const float H = hud_.height();
    const float left = hud_.inset_l() + u * 3.0f;
    const float right = W - hud_.inset_r() - u * 3.0f;
    const float top = hud_.inset_t() + u * 3.0f;
    const float bottom = H - hud_.inset_b() - u * 3.0f;

    // ------------------------------------------------------------- title
    if (phase_ == Phase::Title) {
        float cy = H * 0.34f;
        hud_.text("THE MATRON", W * 0.5f, cy, u * 11.0f, INK, Align::Center, true);
        hud_.text("ST AGNES CHILDREN'S HOSPITAL, CLOSED 1994",
                  W * 0.5f, cy + u * 13.0f, u * 3.0f, DIM, Align::Center);

        float wy = H * 0.60f;
        hud_.text_block("Headphones strongly recommended. Contains sudden loud "
                        "noises, flashing images, and themes of child death.",
                        W * 0.5f, wy, std::min(W * 0.8f, u * 84.0f), u * 3.0f, DIM);

        float pulse = 0.55f + 0.45f * std::sin(time * 2.0f);
        hud_.text("TAP TO BEGIN", W * 0.5f, H * 0.82f, u * 4.2f,
                  v4(INK.x, INK.y, INK.z, pulse), Align::Center);
        hud_.end();
        return;
    }

    // ------------------------------------------------ chapter / death / end
    if (phase_ == Phase::ChapterCard) {
        const story::Chapter& ch = story::CHAPTERS[chapter_ - 1];
        float a = clampf(1.0f - std::abs(phase_time_ - 1.7f) / 1.7f, 0.0f, 1.0f);
        hud_.fade(v4(0, 0, 0, clampf(1.0f - phase_time_ * 0.5f, 0.0f, 1.0f)));
        hud_.text(ch.title, W * 0.5f, H * 0.36f, u * 13.0f,
                  v4(BLOOD.x, BLOOD.y, BLOOD.z, a), Align::Center, true);
        hud_.text(ch.subtitle, W * 0.5f, H * 0.55f, u * 4.6f,
                  v4(INK.x, INK.y, INK.z, a), Align::Center);
        hud_.end();
        return;
    }

    if (phase_ == Phase::Dead) {
        hud_.fade(v4(0.05f, 0.0f, 0.0f, clampf(phase_time_ * 0.5f, 0, 0.72f)));
        hud_.text("SHE FOUND YOU", W * 0.5f, H * 0.38f, u * 8.0f, BLOOD,
                  Align::Center, true);
        hud_.text_block(death_reason_, W * 0.5f, H * 0.52f,
                        std::min(W * 0.8f, u * 76.0f), u * 3.4f, DIM);
        if (phase_time_ > 2.6f) {
            float pulse = 0.5f + 0.5f * std::sin(time * 2.4f);
            hud_.text("TAP TO TRY AGAIN", W * 0.5f, H * 0.74f, u * 4.0f,
                      v4(INK.x, INK.y, INK.z, pulse), Align::Center);
        }
        hud_.end();
        return;
    }

    if (phase_ == Phase::Ending) {
        hud_.fade(v4(0, 0, 0, 0.75f));
        hud_.text("OUT", W * 0.5f, H * 0.24f, u * 10.0f, INK, Align::Center, true);
        hud_.text_block(
            "You come up through the flue into the car park at four in the "
            "morning, and the boy will not let go of your sleeve.\n\n"
            "Behind you, eleven floors down and eleven names long, somebody "
            "finishes her rounds and starts them again.",
            W * 0.5f, H * 0.40f, std::min(W * 0.82f, u * 86.0f), u * 3.6f, INK);
        if (phase_time_ > 6.0f) {
            hud_.text("TAP", W * 0.5f, H * 0.86f, u * 3.6f, DIM, Align::Center);
        }
        hud_.end();
        return;
    }

    if (phase_ == Phase::Paused) {
        hud_.fade(v4(0, 0, 0, 0.72f));
        hud_.text("PAUSED", W * 0.5f, H * 0.40f, u * 8.0f, INK, Align::Center, true);
        hud_.text("TAP TO RESUME", W * 0.5f, H * 0.56f, u * 4.0f, DIM, Align::Center);
        hud_.end();
        return;
    }

    // -------------------------------------------------------------- in game

    // Torch charge, top left.
    {
        float bw = std::min(W * 0.22f, u * 26.0f);
        float bh = u * 1.5f;
        hud_.text("TORCH", left, top, u * 2.6f, DIM);
        float y = top + u * 3.6f;
        hud_.rect(left, y, bw, bh, v4(0.10f, 0.09f, 0.09f, 0.75f));
        v4 c = player_.torch_charge < 0.2f ? BLOOD : v4(0.80f, 0.70f, 0.36f, 0.95f);
        hud_.rect(left, y, bw * player_.torch_charge, bh, c);
        if (!player_.torch_on) {
            hud_.text("OFF", left + bw + u * 1.6f, top + u * 3.0f, u * 2.6f, DIM);
        }
    }

    // Stamina, only while it matters.
    if (player_.stamina < 0.999f) {
        float bw = std::min(W * 0.22f, u * 26.0f);
        float bh = u * 1.0f;
        float y = top + u * 6.4f;
        hud_.rect(left, y, bw, bh, v4(0.10f, 0.09f, 0.09f, 0.6f));
        hud_.rect(left, y, bw * player_.stamina, bh, v4(0.55f, 0.58f, 0.62f, 0.8f));
    }

    // Chapter marker, top right.
    {
        const story::Chapter& ch = story::CHAPTERS[chapter_ - 1];
        hud_.text(ch.subtitle, right, top, u * 2.8f, DIM, Align::Right);
    }

    // Breath, only while hiding — it replaces the stamina bar's job.
    if (player_.hiding) {
        float bw = std::min(W * 0.42f, u * 46.0f);
        float bh = u * 1.8f;
        float x = W * 0.5f - bw * 0.5f;
        float y = bottom - u * 20.0f;
        hud_.text(player_.holding_breath ? "HOLDING BREATH" : "BREATHING",
                  W * 0.5f, y - u * 4.0f, u * 3.2f,
                  player_.holding_breath ? BLOOD : DIM, Align::Center);
        hud_.rect(x, y, bw, bh, v4(0.10f, 0.09f, 0.09f, 0.8f));
        hud_.rect(x, y, bw * player_.breath, bh, BLOOD);
    }

    // Objective, centred, wrapped, and only for a while after it changes.
    if (objective_ && objective_[0] && objective_timer_ > 0) {
        float a = clampf(objective_timer_, 0.0f, 1.0f) * 0.8f;
        hud_.text_block(objective_, W * 0.5f, H * 0.20f,
                        std::min(W * 0.7f, u * 72.0f), u * 3.4f,
                        v4(INK.x, INK.y, INK.z, a));
    }

    // Subtitle: speaker tag above, line wrapped below, both clamped.
    if (subtitle_timer_ > 0 && !subtitle_.empty()) {
        float a = clampf(subtitle_timer_, 0.0f, 1.0f);
        float y = bottom - u * 13.0f;
        float max_w = std::min(W * 0.78f, u * 80.0f);
        hud_.rect(W * 0.5f - max_w * 0.5f - u, y - u * 5.0f,
                  max_w + u * 2.0f, u * 12.0f, v4(0, 0, 0, 0.45f * a));
        hud_.text_block(subtitle_, W * 0.5f, y, max_w, u * 3.4f,
                        v4(INK.x, INK.y, INK.z, a));
    }

    // ---- touch controls
    const float stick_r = std::min(W * 0.11f, u * 13.0f);
    const float stick_cx = left + stick_r * 1.6f;
    const float stick_cy = bottom - stick_r * 1.6f;
    hud_.ring(stick_cx, stick_cy, stick_r, u * 0.35f, v4(0.8f, 0.78f, 0.74f, 0.16f));
    hud_.circle(stick_cx, stick_cy, stick_r * 0.34f, v4(0.8f, 0.78f, 0.74f, 0.12f));

    const float btn_r = std::min(W * 0.075f, u * 9.0f);
    const float bx = right - btn_r * 1.2f;
    const float by = bottom - btn_r * 1.2f;

    // Run (bottom right), crouch above it, torch above that.
    hud_.ring(bx, by, btn_r, u * 0.35f, v4(0.8f, 0.78f, 0.74f, 0.20f));
    hud_.text("RUN", bx, by - u * 1.4f, u * 2.6f, DIM, Align::Center);

    float cy2 = by - btn_r * 2.5f;
    hud_.ring(bx, cy2, btn_r, u * 0.35f, v4(0.8f, 0.78f, 0.74f, 0.20f));
    hud_.text(player_.hiding ? "HOLD" : "CROUCH", bx, cy2 - u * 1.4f, u * 2.2f,
              DIM, Align::Center);

    float ty = by - btn_r * 5.0f;
    hud_.ring(bx, ty, btn_r, u * 0.35f,
              player_.torch_on ? v4(0.85f, 0.75f, 0.40f, 0.35f)
                               : v4(0.8f, 0.78f, 0.74f, 0.18f));
    hud_.text("TORCH", bx, ty - u * 1.4f, u * 2.2f, DIM, Align::Center);

    // Interact prompt: centre-bottom, only when something is in reach.
    if (interact_prompt_) {
        float pw = std::min(W * 0.34f, u * 38.0f);
        float ph = u * 8.0f;
        float px = W * 0.5f - pw * 0.5f;
        float py = bottom - btn_r * 1.2f - ph * 0.5f;
        float pulse = 0.6f + 0.4f * std::sin(time * 3.4f);
        hud_.rect(px, py, pw, ph, v4(0.06f, 0.05f, 0.05f, 0.66f));
        hud_.rect_outline(px, py, pw, ph, u * 0.28f,
                          v4(BLOOD.x, BLOOD.y, BLOOD.z, 0.35f + pulse * 0.4f));
        hud_.text(interact_label_, W * 0.5f, py + ph * 0.5f - u * 1.7f,
                  u * 3.2f, INK, Align::Center);
    }

    // Pause, tucked into the top-right corner inside the safe area.
    hud_.text("| |", right, top + u * 5.0f, u * 3.2f, DIM, Align::Right);

    // Fear: a red pulse at the edges rather than a number.
    if (player_.fear > 0.35f) {
        float a = (player_.fear - 0.35f) * 0.5f *
                  (0.7f + 0.3f * std::sin(time * (3.0f + player_.fear * 6.0f)));
        hud_.rect(0, 0, W, u * 2.0f, v4(0.5f, 0.05f, 0.04f, a));
        hud_.rect(0, H - u * 2.0f, W, u * 2.0f, v4(0.5f, 0.05f, 0.04f, a));
    }

    hud_.end();
}

}  // namespace hl
