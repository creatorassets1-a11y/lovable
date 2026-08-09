// Game state and rules.
#pragma once
#include <string>
#include <vector>

#include "../audio/audio.h"
#include "../core/hmath.h"
#include "../gfx/renderer.h"
#include "hud.h"
#include "level.h"
#include "story.h"

namespace hl {

using namespace hm;

// -------------------------------------------------------------------- input
//
// Filled by the platform layer every frame. Everything is already normalised so
// the game never has to know about screen pixels.

struct Input {
    v2 move;              // -1..1 each axis, from the left stick
    v2 look;              // radians delta this frame
    bool run = false;
    bool crouch = false;
    bool interact = false;      // edge-triggered by the platform layer
    bool torch = false;         // edge-triggered
    bool pause = false;         // edge-triggered
    bool any_tap = false;       // for menus
};

// ------------------------------------------------------------------- player

struct Player {
    v3 pos;
    float yaw = 0, pitch = 0;
    float height = 1.68f;
    float crouch_blend = 0;
    v3 velocity;

    bool torch_on = true;
    float torch_charge = 1.0f;
    float torch_flicker = 1.0f;

    float stamina = 1.0f;
    float fear = 0.0f;           // 0 calm .. 1 about to fall over
    float noise = 0.0f;          // how loud the player is *right now*

    bool hiding = false;
    v3 hide_spot;
    float breath = 1.0f;
    bool holding_breath = false;

    bool has_boy = false;
    bool alive = true;

    v3 eye() const { return pos + v3(0, height - crouch_blend * 0.62f, 0); }
    v3 forward() const {
        return v3(std::cos(yaw) * std::cos(pitch), std::sin(pitch),
                  std::sin(yaw) * std::cos(pitch));
    }
};

// ------------------------------------------------------------------ Matron
//
// Blind. She navigates from memory and hunts by sound, which is why she can be
// on screen constantly without the tension collapsing: seeing her is not the
// same as being seen.

enum class MatronState { Patrol, Listen, Investigate, Hunt, Grab, Dormant };

struct Matron {
    v3 pos;
    float yaw = 0;
    MatronState state = MatronState::Patrol;
    float timer = 0;
    float speed = 0;
    float aggression = 0.3f;
    float awareness = 0.0f;      // 0..1, builds from heard noise
    v3 heard_at;
    bool has_heard = false;
    int patrol_index = 0;
    float step_timer = 0;
    float vocal_timer = 8.0f;
    std::vector<int16_t> flow;
    int flow_target_x = -1, flow_target_z = -1;
};

struct WardChild {
    v3 pos;
    float yaw = 0;
    float shriek_cooldown = 0;
    bool shrieking = false;
    float anim_time = 0;
    int anim = 0;                // 0 idle, 1 crawl, 2 shriek
};

// -------------------------------------------------------------------- story

struct StoryBeat {
    float at = 0;                // seconds into the chapter, or -1
    const char* trigger_zone = nullptr;
    v3 zone_pos;
    float zone_radius = 0;
    const char* line = nullptr;  // voice id
    const char* objective = nullptr;
    bool fired = false;
};

enum class Phase { Boot, Title, Playing, Paused, Dead, ChapterCard, Ending };

class Game {
public:
    bool init(int width, int height, AudioEngine* audio);
    void resize(int width, int height);
    void update(float dt, const Input& in);
    void render(float time);
    void shutdown();

    void on_pause();
    void on_resume();
    /** Safe-area insets in pixels, forwarded from the Android window. */
    void set_insets(float l, float t, float r, float b) { hud_.set_insets(l, t, r, b); }

    /** Redirect the final composite; the offscreen test harness has no
        default framebuffer to draw into. */
    void set_output_framebuffer(unsigned fbo) { renderer_.set_output_framebuffer(fbo); }
    /** Probes used by the headless smoke test. */
    bool solid_at_player() const { return level_.solid_at(player_.pos.x, player_.pos.z); }
    float matron_distance() const { return length(matron_.pos - player_.pos); }
    /** Where the player currently needs to go, for the headless test bot. */
    bool objective_position(v3* out) const {
        return level_.find_spawn(story::note_taken(chapter_) ? 'X' : 'N', out);
    }

    Phase phase() const { return phase_; }
    const Player& player() const { return player_; }
    int chapter() const { return chapter_; }
    /** Frame cost of the last render, for the adaptive resolution controller. */
    void report_frame_ms(float ms);
    /** Grid-path one step from `from` toward `to`. Used by the test bot. */
    bool navigate_from(v3 from, v3 to, v3* out_dir) const;

private:
    // ---- setup
    bool load_assets();
    void start_chapter(int n);
    void spawn_from_level();

    // ---- simulation
    void update_player(float dt, const Input& in);
    void update_matron(float dt);
    void update_children(float dt);
    void update_story(float dt);
    void update_fear(float dt);
    void emit_noise(v3 at, float loudness);
    /** A scare: stinger, sub-bass, shake, chromatic tear, and a buzz. */
    void scare(float strength, v3 from, bool haptic_heavy);
    void update_scares(float dt);
    void update_fear_audio(float dt);
    void kill_player(const char* reason);
    void say(const char* line_id);

    // ---- drawing
    void build_scene(float time);
    void draw_hud(float time);

    Renderer renderer_;
    Hud hud_;
    AudioEngine* audio_ = nullptr;
    Level level_;

    Player player_;
    Matron matron_;
    std::vector<WardChild> children_;

    Mesh mesh_matron_, mesh_child_, mesh_bed_, mesh_locker_, mesh_gurney_;
    Mesh mesh_iv_, mesh_chair_, mesh_lamp_, mesh_debris_;
    Animator anim_matron_, anim_child_;

    struct MatSet {
        Texture a, n, orm;
        Material mat;
        bool load(const std::string& name, float uv, bool triplanar);
    };
    MatSet m_tile_, m_plaster_, m_concrete_, m_floor_, m_ceiling_;
    MatSet m_metal_, m_fabric_, m_rubble_, m_skin_, m_child_;

    struct Prop {
        const Mesh* mesh;
        const Material* mat;
        mat4 model;
        v3 pos;
        char tag;
        bool used = false;
    };
    std::vector<Prop> props_;
    std::vector<v3> hide_spots_;
    std::vector<v3> patrol_points_;
    std::vector<DrawItem> scene_;
    SceneView view_;
    PostParams post_;

    Phase phase_ = Phase::Boot;
    int chapter_ = 1;
    float chapter_time_ = 0;
    float phase_time_ = 0;
    float shake_ = 0;
    float flash_ = 0;
    float blind_ = 1.0f;
    const char* objective_ = "";
    float objective_timer_ = 0;
    std::string subtitle_;
    float subtitle_timer_ = 0;
    const char* death_reason_ = "";
    Rng rng_;
    int width_ = 0, height_ = 0;
    float frame_avg_ms_ = 16.0f;
    float scale_cooldown_ = 0;
    // Grab sequence: the camera is taken away from the player and pointed at
    // her while she closes, which is the only time the game moves the view.
    float grab_t_ = 0;
    v3 grab_look_from_;
    bool grab_active_ = false;

    float sighting_cooldown_ = 0;
    bool was_visible_ = false;
    int heart_voice_ = -1;
    int breath_voice_ = -1;
    int whisper_voice_ = -1;
    float torch_sway_x_ = 0, torch_sway_y_ = 0;
    float bob_phase_ = 0;
    float bob_amount_ = 0;
    float step_flip_ = 1.0f;

    bool interact_prompt_ = false;
    friend struct HudAccess;
    const char* interact_label_ = "";
};

}  // namespace hl
