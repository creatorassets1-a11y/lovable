#include "game.h"

#include <algorithm>
#include <cstring>

#include "../core/asset.h"
#include "story.h"

namespace hl {

namespace {

constexpr float WALK_SPEED = 2.05f;
constexpr float RUN_SPEED = 4.10f;
constexpr float CROUCH_SPEED = 1.05f;
constexpr float PLAYER_RADIUS = 0.34f;
constexpr float TORCH_LIFE = 300.0f;      // seconds of continuous use
constexpr float GRAB_RANGE = 0.95f;

/** Slide along walls rather than sticking to them. */
void move_slide(const Level& lv, v3* pos, v3 delta, float radius) {
    v3 p = *pos;
    // Resolve axes independently so a glancing wall redirects instead of stopping.
    float nx = p.x + delta.x;
    if (!lv.solid_at(nx + (delta.x > 0 ? radius : -radius), p.z - radius) &&
        !lv.solid_at(nx + (delta.x > 0 ? radius : -radius), p.z + radius)) {
        p.x = nx;
    }
    float nz = p.z + delta.z;
    if (!lv.solid_at(p.x - radius, nz + (delta.z > 0 ? radius : -radius)) &&
        !lv.solid_at(p.x + radius, nz + (delta.z > 0 ? radius : -radius))) {
        p.z = nz;
    }
    *pos = p;
}

}  // namespace

bool Game::MatSet::load(const std::string& name, float uv, bool triplanar) {
    bool ok = a.load("tex/" + name + "_a.png", true);
    ok &= n.load("tex/" + name + "_n.png", false);
    ok &= orm.load("tex/" + name + "_orm.png", false);
    mat.albedo = &a;
    mat.normal = &n;
    mat.orm = &orm;
    mat.uv_scale = v2(uv, uv);
    mat.triplanar = triplanar;
    return ok;
}

// ==================================================================== setup

bool Game::init(int width, int height, AudioEngine* audio) {
    width_ = width;
    height_ = height;
    audio_ = audio;
    rng_ = Rng(0xC0FFEE);

    if (!renderer_.init(width, height)) return false;
    if (!hud_.init()) return false;
    if (!load_assets()) return false;

    phase_ = Phase::Title;
    phase_time_ = 0;
    blind_ = 0.0f;
    return true;
}

bool Game::load_assets() {
    m_tile_.load("wall_tile", 1.0f, false);
    m_plaster_.load("wall_plaster", 1.0f, false);
    m_concrete_.load("concrete", 1.0f, false);
    m_floor_.load("floor_lino", 1.0f, false);
    m_ceiling_.load("ceiling", 1.0f, false);
    m_metal_.load("metal", 1.2f, false);
    m_fabric_.load("fabric", 1.5f, false);
    m_rubble_.load("rubble", 1.0f, false);
    m_skin_.load("skin_burn", 2.8f, true);
    m_child_.load("skin_child", 3.4f, true);

    mesh_matron_.load("mesh/matron.hmsh");
    mesh_child_.load("mesh/child.hmsh");
    mesh_bed_.load("mesh/bed.hmsh");
    mesh_locker_.load("mesh/locker.hmsh");
    mesh_gurney_.load("mesh/gurney.hmsh");
    mesh_iv_.load("mesh/ivstand.hmsh");
    mesh_chair_.load("mesh/wheelchair.hmsh");
    mesh_lamp_.load("mesh/lamp.hmsh");
    mesh_debris_.load("mesh/debris.hmsh");

    anim_matron_.init(&mesh_matron_.bones(), &mesh_matron_.clips());
    anim_child_.init(&mesh_child_.bones(), &mesh_child_.clips());
    anim_matron_.play("idle", 0.0f);
    anim_child_.play("idle", 0.0f);

    if (audio_) {
        for (int i = 0; i < story::SFX_COUNT; i++) {
            const char* n = story::sfx_name(i);
            audio_->load_clip(n, std::string("audio/sfx/") + n + ".ogg");
        }
        for (int i = 0; i < story::VOICE_COUNT; i++) {
            const char* n = story::voice_id(i);
            audio_->load_clip(n, std::string("audio/vo/") + n + ".ogg");
        }
    }
    return true;
}

void Game::resize(int width, int height) {
    width_ = width;
    height_ = height;
    renderer_.resize(width, height);
}

void Game::shutdown() { renderer_.shutdown(); }

void Game::on_pause() {
    if (phase_ == Phase::Playing) phase_ = Phase::Paused;
    if (audio_) audio_->pause();
}

void Game::on_resume() {
    if (audio_) audio_->resume();
}

// ================================================================= chapters

void Game::start_chapter(int n) {
    chapter_ = clampf((float)n, 1.0f, (float)story::CHAPTER_COUNT);
    const story::Chapter& ch = story::CHAPTERS[chapter_ - 1];

    if (!level_.load(ch.level)) {
        loge("chapter %d failed to load level %s", chapter_, ch.level);
        return;
    }
    spawn_from_level();

    chapter_time_ = 0;
    blind_ = 1.0f;
    player_.torch_charge = clampf(1.0f - (chapter_ - 1) * 0.08f, 0.5f, 1.0f);
    player_.fear = 0;
    player_.stamina = 1;
    player_.hiding = false;
    player_.breath = 1;
    player_.alive = true;
    player_.has_boy = false;

    matron_.aggression = ch.aggression;
    matron_.state = ch.matron_dormant ? MatronState::Dormant : MatronState::Patrol;
    matron_.awareness = 0;
    matron_.has_heard = false;
    matron_.flow_target_x = -1;

    objective_ = ch.first_objective;
    objective_timer_ = 11.0f;
    subtitle_.clear();
    subtitle_timer_ = 0;

    story::reset_beats(chapter_);

    if (audio_) {
        audio_->set_bed(ch.bed, 0.55f, 1.5f);
        audio_->set_muffle(0);
    }
    phase_ = Phase::ChapterCard;
    phase_time_ = 0;
}

void Game::spawn_from_level() {
    props_.clear();
    hide_spots_.clear();
    patrol_points_.clear();
    children_.clear();

    v3 p;
    if (level_.find_spawn('P', &p)) player_.pos = v3(p.x, 0, p.z);
    player_.yaw = 0;
    player_.pitch = 0;

    if (level_.find_spawn('M', &p)) matron_.pos = v3(p.x, 0, p.z);
    matron_.yaw = PI;

    for (const auto& s : level_.spawns()) {
        v3 pos = level_.cell_center(s.x, s.z);
        float yaw = rng_.range(0, TAU);
        float quarter = std::round(yaw / (PI * 0.5f)) * PI * 0.5f;

        const Mesh* mesh = nullptr;
        const Material* mat = nullptr;
        float use_yaw = yaw;
        switch (s.tag) {
            case 'B': mesh = &mesh_bed_; mat = &m_fabric_.mat; use_yaw = quarter; break;
            case 'L': mesh = &mesh_locker_; mat = &m_metal_.mat; use_yaw = quarter;
                      hide_spots_.push_back(pos); break;
            case 'G': mesh = &mesh_gurney_; mat = &m_metal_.mat; break;
            case 'I': mesh = &mesh_iv_; mat = &m_metal_.mat; break;
            case 'W': mesh = &mesh_chair_; mat = &m_metal_.mat; break;
            case 'R': mesh = &mesh_debris_; mat = &m_rubble_.mat; break;
            case 'A': mesh = &mesh_lamp_; mat = &m_metal_.mat;
                      pos.y = WALL_H - 0.02f; use_yaw = 0;
                      patrol_points_.push_back(v3(pos.x, 0, pos.z)); break;
            case 'C': {
                WardChild c;
                c.pos = pos;
                c.yaw = yaw;
                c.anim = 0;
                children_.push_back(c);
                break;
            }
            case 'N': case 'X': break;   // markers only, drawn as prompts
            default: break;
        }
        if (mesh) {
            Prop pr;
            pr.mesh = mesh;
            pr.mat = mat;
            pr.pos = pos;
            pr.tag = s.tag;
            pr.model = mat4_translate(pos) * mat4_rotate_y(use_yaw);
            props_.push_back(pr);
        }
    }

    // Patrol route: the lamp positions run the length of every corridor, which
    // is exactly the beat she should be walking.
    if (patrol_points_.size() < 2) {
        for (const auto& s : level_.spawns()) {
            if (s.tag == 'B') patrol_points_.push_back(level_.cell_center(s.x, s.z));
        }
    }
    matron_.patrol_index = 0;
}

// =================================================================== player

void Game::update_player(float dt, const Input& in) {
    Player& p = player_;

    p.yaw += in.look.x;
    p.pitch = clampf(p.pitch - in.look.y, -0.95f, 0.95f);
    while (p.yaw > TAU) p.yaw -= TAU;
    while (p.yaw < 0) p.yaw += TAU;

    float target_crouch = in.crouch ? 1.0f : 0.0f;
    p.crouch_blend = approach(p.crouch_blend, target_crouch, 9.0f, dt);

    if (p.hiding) {
        // Locked in place; the only choice left is whether to breathe.
        p.holding_breath = in.crouch || in.run;
        if (p.holding_breath) {
            p.breath = clampf(p.breath - dt * 0.26f, 0, 1);
            if (p.breath <= 0) {
                p.holding_breath = false;
                emit_noise(p.pos, 1.0f);         // the gasp
                if (audio_) audio_->play("gasp", {0.9f, 1.0f, 0, false, Bus::Sfx});
            }
        } else {
            p.breath = clampf(p.breath + dt * 0.20f, 0, 1);
            emit_noise(p.pos, 0.10f);
        }
        p.noise = p.holding_breath ? 0.0f : 0.10f;
        if (audio_) audio_->set_muffle(0.55f);
        return;
    }

    float mag = std::sqrt(in.move.x * in.move.x + in.move.y * in.move.y);
    bool moving = mag > 0.08f;
    bool running = in.run && !in.crouch && p.stamina > 0.05f && moving;

    float speed = in.crouch ? CROUCH_SPEED : (running ? RUN_SPEED : WALK_SPEED);
    speed *= lerpf(0.55f, 1.0f, 1.0f - p.fear * 0.45f);
    if (story::CHAPTERS[chapter_ - 1].flooded) speed *= 0.72f;

    if (running) p.stamina = clampf(p.stamina - dt * 0.30f, 0, 1);
    else p.stamina = clampf(p.stamina + dt * 0.22f, 0, 1);

    if (moving) {
        v3 fwd(std::cos(p.yaw), 0, std::sin(p.yaw));
        v3 right(-fwd.z, 0, fwd.x);
        v3 dir = normalize(fwd * in.move.y + right * in.move.x);
        move_slide(level_, &p.pos, dir * (speed * clampf(mag, 0, 1) * dt), PLAYER_RADIUS);
    }

    // Noise: what she actually hears. Crouching is nearly silent; running in
    // standing water is the loudest thing in the building.
    float noise = 0.0f;
    if (moving) {
        noise = in.crouch ? 0.12f : (running ? 0.95f : 0.42f);
        if (story::CHAPTERS[chapter_ - 1].flooded) noise = std::min(1.0f, noise * 1.7f + 0.2f);
    }
    p.noise = noise;
    if (noise > 0.05f) emit_noise(p.pos, noise);

    // Footsteps, spaced by speed.
    static float step_acc = 0;
    step_acc += moving ? dt * (running ? 2.6f : (in.crouch ? 1.1f : 1.7f)) : 0.0f;
    if (step_acc >= 1.0f) {
        step_acc = 0;
        if (audio_) {
            bool wet = story::CHAPTERS[chapter_ - 1].flooded;
            char name[16];
            snprintf(name, sizeof(name), "%s%d", wet ? "step_wet" : "step_dry", 1 + rng_.i(4));
            PlayParams pp;
            pp.gain = in.crouch ? 0.18f : (running ? 0.5f : 0.32f);
            pp.pitch = 0.94f + rng_.f() * 0.14f;
            audio_->play(name, pp);
        }
    }

    // Torch.
    if (p.torch_on && p.torch_charge > 0) {
        p.torch_charge = clampf(p.torch_charge - dt / TORCH_LIFE, 0, 1);
        if (p.torch_charge <= 0) p.torch_on = false;
    }
    float flick = 1.0f;
    if (p.torch_charge < 0.22f && p.torch_on) {
        flick = (rng_.f() < 0.06f) ? 0.15f : (0.72f + rng_.f() * 0.28f);
    }
    p.torch_flicker = approach(p.torch_flicker, flick, 22.0f, dt);
    if (audio_) audio_->set_muffle(p.fear * 0.30f);
}

void Game::emit_noise(v3 at, float loudness) {
    // A noise is only heard if it is loud enough at her position. Walls do not
    // block it outright, they just cost distance.
    float d = length(matron_.pos - at);
    float heard = loudness * (1.0f - clampf(d / (9.0f + loudness * 16.0f), 0, 1));
    if (!level_.line_of_sight(matron_.pos, at)) heard *= 0.55f;
    if (heard > 0.04f) {
        matron_.awareness = clampf(matron_.awareness + heard * 0.9f, 0, 1.6f);
        matron_.heard_at = at;
        matron_.has_heard = true;
    }
}

// =================================================================== Matron

void Game::update_matron(float dt) {
    Matron& m = matron_;
    if (m.state == MatronState::Dormant) return;

    m.timer -= dt;
    m.vocal_timer -= dt;
    // Awareness bleeds away, but never all the way once she is hunting.
    float decay = (m.state == MatronState::Hunt) ? 0.06f : 0.22f;
    m.awareness = clampf(m.awareness - decay * dt, 0, 1.6f);

    float dist_to_player = length(player_.pos - m.pos);

    // ---- state transitions
    switch (m.state) {
        case MatronState::Patrol:
            m.speed = 0.85f + m.aggression * 0.35f;
            if (m.timer <= 0) {
                m.state = MatronState::Listen;
                m.timer = 1.6f + rng_.f() * 2.2f;
            }
            if (m.awareness > 0.55f) {
                m.state = MatronState::Investigate;
                m.timer = 9.0f;
            }
            break;

        case MatronState::Listen:
            m.speed = 0;
            if (m.awareness > 0.45f) {
                m.state = MatronState::Investigate;
                m.timer = 9.0f;
            } else if (m.timer <= 0) {
                m.state = MatronState::Patrol;
                m.timer = 7.0f + rng_.f() * 6.0f;
            }
            break;

        case MatronState::Investigate:
            m.speed = 1.25f + m.aggression * 0.5f;
            // Below this aggression she investigates but never commits. Chapter
            // one is a lesson in how she works, not a fight you can lose.
            if (m.aggression >= 0.25f &&
                (m.awareness > 1.0f ||
                 (dist_to_player < 3.2f && player_.noise > 0.35f))) {
                m.state = MatronState::Hunt;
                m.timer = 12.0f;
                if (audio_) audio_->play("stinger_a", {0.7f});
                say("m_04");
            } else if (m.timer <= 0 ||
                       length(m.heard_at - m.pos) < 1.2f) {
                m.state = MatronState::Listen;
                m.timer = 2.5f;
                m.has_heard = false;
            }
            break;

        case MatronState::Hunt:
            // Player walk is 2.05 and run is 4.10: she always beats a walk and
            // never quite beats a sprint, so escaping costs you noise.
            m.speed = 1.95f + m.aggression * 1.45f;
            if (dist_to_player < GRAB_RANGE && !player_.hiding) {
                m.state = MatronState::Grab;
                m.timer = 1.2f;
            } else if (m.timer <= 0 && m.awareness < 0.35f) {
                m.state = MatronState::Investigate;
                m.timer = 6.0f;
            }
            break;

        case MatronState::Grab:
            m.speed = 0;
            if (m.timer <= 0) kill_player("She found you.");
            break;

        default: break;
    }

    // ---- movement toward the current goal
    v3 goal = m.pos;
    if (m.state == MatronState::Patrol) {
        if (!patrol_points_.empty()) {
            goal = patrol_points_[m.patrol_index % patrol_points_.size()];
            if (length(goal - m.pos) < 1.4f) {
                m.patrol_index = (m.patrol_index + 1) % (int)patrol_points_.size();
            }
        }
    } else if (m.state == MatronState::Investigate) {
        goal = m.has_heard ? m.heard_at : player_.pos;
    } else if (m.state == MatronState::Hunt) {
        goal = player_.pos;
    }

    if (m.speed > 0.01f) {
        int gx = (int)(goal.x / CELL), gz = (int)(goal.z / CELL);
        if (gx != m.flow_target_x || gz != m.flow_target_z) {
            level_.build_flow(gx, gz, &m.flow);
            m.flow_target_x = gx;
            m.flow_target_z = gz;
        }
        int cx = (int)(m.pos.x / CELL), cz = (int)(m.pos.z / CELL);
        int nx, nz;
        v3 step_target = goal;
        if (level_.flow_step(m.flow, cx, cz, &nx, &nz)) {
            step_target = level_.cell_center(nx, nz);
        }
        v3 dir = step_target - m.pos;
        dir.y = 0;
        if (length(dir) > 0.001f) {
            dir = normalize(dir);
            m.yaw = std::atan2(dir.z, dir.x);
            move_slide(level_, &m.pos, dir * (m.speed * dt), 0.36f);
        }

        m.step_timer -= dt * m.speed;
        if (m.step_timer <= 0) {
            m.step_timer = 0.55f;
            if (audio_) {
                char name[16];
                bool wet = story::CHAPTERS[chapter_ - 1].flooded;
                snprintf(name, sizeof(name), "%s%d", wet ? "step_wet" : "step_dry", 1 + rng_.i(4));
                PlayParams pp;
                pp.gain = 1.0f;
                pp.pitch = 0.78f + rng_.f() * 0.08f;
                pp.positional = true;
                pp.pos = m.pos;
                pp.range = 26.0f;
                audio_->play(name, pp);
            }
        }
    }

    // ---- she talks to herself, on her rounds
    if (m.vocal_timer <= 0) {
        m.vocal_timer = (m.state == MatronState::Hunt ? 4.0f : 13.0f) + rng_.f() * 8.0f;
        const char* line = story::matron_line(m.state == MatronState::Hunt, rng_.next());
        if (line && audio_) {
            PlayParams pp;
            pp.gain = 1.0f;
            pp.positional = true;
            pp.pos = m.pos;
            pp.range = 30.0f;
            pp.bus = Bus::Voice;
            audio_->play(line, pp);
            subtitle_ = story::subtitle_for(line);
            subtitle_timer_ = 3.4f;
        }
    }

    // ---- animation selection
    const char* clip = "idle";
    switch (m.state) {
        case MatronState::Patrol: clip = "walk"; break;
        case MatronState::Listen: clip = "listen"; break;
        case MatronState::Investigate: clip = "walk"; break;
        case MatronState::Hunt: clip = "chase"; break;
        case MatronState::Grab: clip = "scream"; break;
        default: clip = "idle"; break;
    }
    anim_matron_.play(clip, 0.28f);
    anim_matron_.set_speed(m.state == MatronState::Investigate ? 1.35f : 1.0f);
    anim_matron_.update(dt);
}

// ================================================================= children

void Game::update_children(float dt) {
    for (auto& c : children_) {
        c.shriek_cooldown -= dt;
        float d = length(player_.pos - c.pos);

        if (c.shrieking) {
            c.anim_time -= dt;
            if (c.anim_time <= 0) {
                c.shrieking = false;
                c.anim = 0;
            }
            continue;
        }

        // They are harmless. What they do is tell her exactly where you are.
        bool sees = d < 5.0f && !player_.hiding &&
                    level_.line_of_sight(c.pos + v3(0, 0.4f, 0),
                                         player_.eye());
        if (sees && c.shriek_cooldown <= 0) {
            c.shrieking = true;
            c.anim = 2;
            c.anim_time = 1.4f;
            c.shriek_cooldown = 12.0f;
            emit_noise(c.pos, 1.4f);
            player_.fear = clampf(player_.fear + 0.16f, 0, 1);
            if (audio_) {
                PlayParams pp;
                pp.gain = 1.0f;
                pp.positional = true;
                pp.pos = c.pos;
                pp.range = 24.0f;
                audio_->play("child_shriek", pp);
            }
        } else if (d < 9.0f && d > 1.2f) {
            // Crawls toward you, slowly, and stops when you look away.
            c.anim = 1;
            v3 dir = normalize(player_.pos - c.pos);
            c.yaw = std::atan2(dir.z, dir.x);
            move_slide(level_, &c.pos, dir * (0.42f * dt), 0.26f);
        } else {
            c.anim = 0;
        }
    }

    // One shared animator: they are always doing roughly the same thing, and
    // twenty skinning palettes per frame is not worth the fidelity.
    int dominant = 0;
    for (const auto& c : children_) dominant = std::max(dominant, c.anim);
    anim_child_.play(dominant == 2 ? "shriek" : (dominant == 1 ? "crawl" : "idle"), 0.2f);
    anim_child_.update(dt);
}

// ==================================================================== story

void Game::say(const char* line_id) {
    if (!line_id) return;
    if (audio_) audio_->speak(line_id, 1.0f);
    subtitle_ = story::subtitle_for(line_id);
    subtitle_timer_ = story::duration_for(line_id) + 0.9f;
}

void Game::update_story(float dt) {
    chapter_time_ += dt;
    const story::Chapter& ch = story::CHAPTERS[chapter_ - 1];

    // Timed and proximity beats.
    const char* line = nullptr;
    const char* obj = nullptr;
    if (story::poll_beats(chapter_, chapter_time_, player_.pos, &line, &obj)) {
        if (line) say(line);
        if (obj) { objective_ = obj; objective_timer_ = 9.0f; }
    }
    if (objective_timer_ > 0) objective_timer_ -= dt;

    // Interaction targets: the document, then the way out.
    interact_prompt_ = false;
    v3 note_pos, exit_pos;
    bool has_note = level_.find_spawn('N', &note_pos);
    bool has_exit = level_.find_spawn('X', &exit_pos);

    if (has_note && !story::note_taken(chapter_) &&
        length(player_.pos - note_pos) < 1.8f) {
        interact_prompt_ = true;
        interact_label_ = ch.note_label;
    } else if (has_exit && story::note_taken(chapter_) &&
               length(player_.pos - exit_pos) < 2.0f) {
        interact_prompt_ = true;
        interact_label_ = "LEAVE";
    } else if (!player_.hiding) {
        for (const auto& h : hide_spots_) {
            if (length(player_.pos - h) < 1.5f) {
                interact_prompt_ = true;
                interact_label_ = "HIDE";
                break;
            }
        }
    } else {
        interact_prompt_ = true;
        interact_label_ = "COME OUT";
    }

    if (subtitle_timer_ > 0) subtitle_timer_ -= dt;
}

void Game::update_fear(float dt) {
    float d = length(matron_.pos - player_.pos);
    bool dark = !player_.torch_on || player_.torch_charge <= 0;

    float rise = 0;
    if (d < 12.0f) rise += (12.0f - d) * 0.016f;
    if (matron_.state == MatronState::Hunt) rise += 0.20f;
    if (dark) rise += 0.05f;
    if (player_.hiding) rise += 0.04f;
    float fall = (d > 14.0f && !dark) ? 0.10f : 0.0f;

    player_.fear = clampf(player_.fear + (rise - fall) * dt, 0, 1);

    if (audio_) {
        audio_->set_bus_gain(Bus::Music, 0.4f + player_.fear * 0.5f);
        if (player_.fear > 0.55f) {
            audio_->set_bed(matron_.state == MatronState::Hunt ? "drone_chaos" : "drone_dread",
                            0.35f + player_.fear * 0.4f, 1.2f);
        }
    }
}

void Game::kill_player(const char* reason) {
    if (!player_.alive) return;
    player_.alive = false;
    death_reason_ = reason;
    phase_ = Phase::Dead;
    phase_time_ = 0;
    shake_ = 1.0f;
    flash_ = 0.6f;
    if (audio_) {
        audio_->play("scream_matron", {1.0f});
        audio_->play("sub_boom", {0.9f});
        audio_->set_bed("drone_chaos", 0.0f, 0.5f);
    }
}

// =================================================================== update

void Game::update(float dt, const Input& in) {
    phase_time_ += dt;
    dt = clampf(dt, 0.0005f, 0.05f);

    switch (phase_) {
        case Phase::Title:
            if (in.any_tap || in.interact) start_chapter(1);
            return;

        case Phase::ChapterCard:
            blind_ = approach(blind_, 0.0f, 1.1f, dt);
            if (phase_time_ > 3.4f || in.any_tap) {
                phase_ = Phase::Playing;
                phase_time_ = 0;
            }
            return;

        case Phase::Paused:
            if (in.pause || in.any_tap) {
                phase_ = Phase::Playing;
                if (audio_) audio_->resume();
            }
            return;

        case Phase::Dead:
            shake_ = approach(shake_, 0, 2.0f, dt);
            flash_ = approach(flash_, 0, 3.0f, dt);
            blind_ = approach(blind_, 0.55f, 0.8f, dt);
            if (phase_time_ > 2.6f && (in.any_tap || in.interact)) start_chapter(chapter_);
            return;

        case Phase::Ending:
            blind_ = approach(blind_, 0.0f, 0.4f, dt);
            if (phase_time_ > 6.0f && in.any_tap) {
                phase_ = Phase::Title;
                phase_time_ = 0;
            }
            return;

        default: break;
    }

    if (in.pause) {
        phase_ = Phase::Paused;
        if (audio_) audio_->pause();
        return;
    }

    blind_ = approach(blind_, 0.0f, 1.4f, dt);
    shake_ = approach(shake_, 0, 2.5f, dt);
    flash_ = approach(flash_, 0, 4.0f, dt);

    if (in.torch && player_.torch_charge > 0) {
        player_.torch_on = !player_.torch_on;
        if (audio_) audio_->play("click", {0.4f});
    }

    if (in.interact) {
        v3 note_pos, exit_pos;
        const story::Chapter& ch = story::CHAPTERS[chapter_ - 1];
        if (player_.hiding) {
            player_.hiding = false;
            if (audio_) { audio_->play("locker", {0.6f}); audio_->set_muffle(0); }
        } else if (level_.find_spawn('N', &note_pos) && !story::note_taken(chapter_) &&
                   length(player_.pos - note_pos) < 1.8f) {
            story::take_note(chapter_);
            objective_ = ch.second_objective;
            objective_timer_ = 11.0f;
            say(ch.note_line);
        } else if (level_.find_spawn('X', &exit_pos) && story::note_taken(chapter_) &&
                   length(player_.pos - exit_pos) < 2.0f) {
            if (chapter_ >= story::CHAPTER_COUNT) {
                phase_ = Phase::Ending;
                phase_time_ = 0;
                if (audio_) { audio_->set_bed("ending", 0.7f, 2.0f); audio_->stop_speech(); }
            } else {
                start_chapter(chapter_ + 1);
            }
            return;
        } else {
            for (const auto& h : hide_spots_) {
                if (length(player_.pos - h) < 1.5f) {
                    player_.hiding = true;
                    player_.pos = v3(h.x, 0, h.z);
                    player_.breath = 1.0f;
                    if (audio_) audio_->play("locker", {0.6f});
                    break;
                }
            }
        }
    }

    update_player(dt, in);
    update_matron(dt);
    update_children(dt);
    update_story(dt);
    update_fear(dt);

    if (audio_) {
        v3 f = player_.forward();
        audio_->set_listener(player_.eye(), f, v3(0, 1, 0));
    }
}

// =================================================================== render

void Game::build_scene(float time) {
    scene_.clear();

    scene_.push_back({&level_.walls(), mat4_identity(), &m_tile_.mat, nullptr, true});
    scene_.push_back({&level_.floor(), mat4_identity(), &m_floor_.mat, nullptr, false});
    scene_.push_back({&level_.ceiling(), mat4_identity(), &m_ceiling_.mat, nullptr, true});
    scene_.push_back({&level_.trim(), mat4_identity(), &m_plaster_.mat, nullptr, true});

    const v3 eye = player_.eye();
    for (const auto& pr : props_) {
        // Cheap distance cull; the corridors are long and mostly unlit.
        if (length2(pr.pos - eye) > 34.0f * 34.0f) continue;
        scene_.push_back({pr.mesh, pr.model, pr.mat, nullptr, pr.tag != 'A'});
    }

    if (matron_.state != MatronState::Dormant) {
        DrawItem m;
        m.mesh = &mesh_matron_;
        m.material = &m_skin_.mat;
        m.model = mat4_translate(matron_.pos) * mat4_rotate_y(-matron_.yaw + PI * 0.5f);
        m.animator = &anim_matron_;
        scene_.push_back(m);
    }

    for (const auto& c : children_) {
        if (length2(c.pos - eye) > 26.0f * 26.0f) continue;
        DrawItem d;
        d.mesh = &mesh_child_;
        d.material = &m_child_.mat;
        d.model = mat4_translate(c.pos) * mat4_rotate_y(-c.yaw + PI * 0.5f);
        d.animator = &anim_child_;
        scene_.push_back(d);
    }

    // ---- view
    const story::Chapter& ch = story::CHAPTERS[chapter_ - 1];
    v3 fwd = player_.forward();
    // Head bob and a fear tremor, both small enough to feel rather than see.
    float bob = std::sin(time * 7.4f) * 0.012f * (player_.noise > 0.2f ? 1.0f : 0.25f);
    float tremor = player_.fear * 0.006f;
    v3 e = eye + v3(0, bob, 0) +
           v3(std::sin(time * 23.0f) * tremor, std::cos(time * 19.0f) * tremor, 0);

    view_.cam_pos = e;
    view_.view = mat4_look_at(e, e + fwd, v3(0, 1, 0));
    view_.proj = mat4_perspective(radians(72.0f),
                                  (float)width_ / (float)std::max(1, height_), 0.05f, 55.0f);

    float torch = player_.torch_on ? player_.torch_flicker : 0.0f;
    view_.torch_pos = e + v3(0, -0.10f, 0) + fwd * 0.15f;
    view_.torch_dir = fwd;
    view_.torch_intensity = 40.0f * torch;
    view_.torch_range = 20.0f;
    view_.torch_color = v3(1.0f, 0.94f, 0.82f);

    view_.ambient = ch.ambient;
    view_.fog_color = ch.fog_color;
    view_.fog_density = ch.fog_density;

    view_.points.clear();
    // A couple of surviving emergency lamps nearest the player.
    int added = 0;
    for (const auto& pr : props_) {
        if (pr.tag != 'A' || added >= 3) continue;
        if (length2(pr.pos - e) > 13.0f * 13.0f) continue;
        PointLight pl;
        pl.pos = pr.pos - v3(0, 0.16f, 0);
        pl.radius = 8.0f;
        pl.color = ch.lamp_color;
        // Failing tubes: a slow flicker keyed off position so they differ.
        float f = 0.55f + 0.45f * std::sin(time * 6.1f + pr.pos.x * 3.7f);
        pl.intensity = ch.lamp_intensity * (f > 0.35f ? 1.0f : 0.15f);
        view_.points.push_back(pl);
        added++;
    }

    // ---- grade
    post_.exposure = 1.45f;
    post_.bloom = 0.55f;
    post_.grain = 0.045f + player_.fear * 0.06f;
    post_.vignette = 0.30f + player_.fear * 0.5f;
    post_.desat = player_.fear * 0.45f;
    post_.red_shift = clampf((player_.fear - 0.55f) * 1.1f, 0, 0.45f);
    post_.chroma = player_.fear * 0.35f + (phase_ == Phase::Dead ? 0.6f : 0.0f);
    post_.glitch = (phase_ == Phase::Dead ? 0.8f : 0.0f) +
                   (matron_.state == MatronState::Grab ? 0.5f : 0.0f);
    post_.flash = flash_;
    post_.blind = blind_;
    post_.scanline = 0.010f;
}

void Game::render(float time) {
    if (phase_ == Phase::Title) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width_, height_);
        glClearColor(0.012f, 0.011f, 0.013f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        draw_hud(time);
        return;
    }

    build_scene(time);

    if (shake_ > 0.001f) {
        float s = shake_ * 0.06f;
        view_.view = mat4_look_at(
            view_.cam_pos + v3(rng_.range(-s, s), rng_.range(-s, s), rng_.range(-s, s)),
            view_.cam_pos + player_.forward(), v3(0, 1, 0));
    }

    renderer_.render(view_, scene_, post_, time);
    draw_hud(time);
}

bool Game::navigate_from(v3 from, v3 to, v3* out_dir) const {
    std::vector<int16_t> flow;
    level_.build_flow((int)(to.x / CELL), (int)(to.z / CELL), &flow);
    int nx, nz;
    if (!level_.flow_step(flow, (int)(from.x / CELL), (int)(from.z / CELL), &nx, &nz)) {
        v3 d = to - from;
        d.y = 0;
        if (length(d) < 0.1f) return false;
        *out_dir = normalize(d);
        return true;
    }
    v3 d = level_.cell_center(nx, nz) - from;
    d.y = 0;
    if (length(d) < 0.001f) return false;
    *out_dir = normalize(d);
    return true;
}

void Game::report_frame_ms(float ms) {
    frame_avg_ms_ = frame_avg_ms_ * 0.92f + ms * 0.08f;
    scale_cooldown_ -= ms * 0.001f;
    if (scale_cooldown_ > 0) return;
    // Hold 60fps by trading internal resolution, never frame rate.
    float s = renderer_.render_scale();
    if (frame_avg_ms_ > 19.5f && s > 0.55f) {
        renderer_.set_render_scale(s - 0.08f);
        scale_cooldown_ = 1.5f;
    } else if (frame_avg_ms_ < 12.0f && s < 1.0f) {
        renderer_.set_render_scale(s + 0.05f);
        scale_cooldown_ = 2.5f;
    }
}

}  // namespace hl
