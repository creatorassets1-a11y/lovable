// controls_test.cpp - does the game move and point where the player meant?
//
// Every check here is phrased in terms of what ends up on screen, not in terms
// of the maths. "Strafe right" is verified by transforming the movement into
// the same view matrix the renderer uses and confirming the player went toward
// screen-right; "look right" is verified by confirming a point ahead of you
// slides to the left of the screen afterwards. That framing matters, because
// the bug this file was written for was a basis vector that was self
// consistent, internally sensible, and exactly backwards.
#include "../app/src/main/cpp/player.h"
#include "../app/src/main/cpp/world.h"
#include "../app/src/main/cpp/audio.h"
#include "../app/src/main/cpp/hud_math.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace hm;

static int gFails = 0, gChecks = 0;
static void check(bool cond, const char* what) {
    gChecks++;
    if (!cond) { gFails++; std::printf("  FAIL  %s\n", what); }
}

// The renderer builds its view matrix with mat4::lookAt, whose first row is
// cross(forward, up). A world point with positive view-space x is drawn on the
// right-hand side of the screen. This is the ground truth for "right".
static float viewSpaceX(const vec3& eye, float yaw, float pitch, const vec3& worldPoint) {
    vec3 fwd(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
    mat4 view = mat4::lookAt(eye, eye + fwd, vec3(0, 1, 0));
    return view.transformPoint(worldPoint).x;
}

static float viewSpaceY(const vec3& eye, float yaw, float pitch, const vec3& worldPoint) {
    vec3 fwd(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
    mat4 view = mat4::lookAt(eye, eye + fwd, vec3(0, 1, 0));
    return view.transformPoint(worldPoint).y;
}

// An open field, so movement tests are never fighting a wall.
static World& openWorld() {
    static World w;
    static bool built = false;
    if (!built) {
        w.W = 64; w.H = 64; w.cellSize = 2.0f;
        w.cells.assign((size_t)w.W * w.H, CELL_ROAD);
        w.groundMat.assign((size_t)w.W * w.H, 0);
        w.buildingOf.assign((size_t)w.W * w.H, 0xFFFF);
        for (int i = 0; i < w.W; i++) {
            w.cells[i] = CELL_SOLID;
            w.cells[(size_t)(w.H - 1) * w.W + i] = CELL_SOLID;
            w.cells[(size_t)i * w.W] = CELL_SOLID;
            w.cells[(size_t)i * w.W + w.W - 1] = CELL_SOLID;
        }
        w.playerStart = w.cellCenter(32, 32);
        built = true;
    }
    return w;
}

int main() {
    std::printf("=== HOLLOW SIGNAL - control tests ===\n");
    World& w = openWorld();
    AudioEngine audio;
    audio.start();
    const float dt = 1.0f / 60.0f;
    const float yaws[] = {0.0f, 0.7f, 1.9f, -2.4f, PI, -PI * 0.5f, 2.9f};

    // --- the basis itself -------------------------------------------------
    std::printf("\n[basis] the camera, the mixer and the controller must agree\n");
    for (float yaw : yaws) {
        Player p;
        p.reset(w.playerStart);
        p.yaw = yaw;
        vec3 f = p.forward();
        vec3 r = p.right();
        vec3 expect = normalize(cross(f, vec3(0, 1, 0)));
        float agree = dot(r, expect);
        if (std::fabs(yaw) < 0.01f)
            std::printf("  yaw %5.2f: right=(%.2f,%.2f,%.2f) expected=(%.2f,%.2f,%.2f)\n",
                        yaw, r.x, r.y, r.z, expect.x, expect.y, expect.z);
        check(agree > 0.999f, "right() equals cross(forward, up), which is screen-right");
        check(std::fabs(dot(r, f)) < 0.001f, "right is perpendicular to forward");
        check(std::fabs(length(r) - 1.0f) < 0.001f, "right is unit length");
    }

    // --- strafing ---------------------------------------------------------
    std::printf("\n[strafe] pushing the stick right must move you toward screen-right\n");
    for (float yaw : yaws) {
        Player p;
        p.reset(w.playerStart);
        p.yaw = yaw;
        vec3 before = p.pos;
        p.moveInput = vec2(1.0f, 0.0f);          // stick fully right
        for (int i = 0; i < 12; i++) p.update(dt, w, audio, 100.0f, false);
        vec3 moved = p.pos - before;

        float sx = viewSpaceX(before + vec3(0, 1.6f, 0), yaw, 0.0f,
                              before + moved + vec3(0, 1.6f, 0));
        if (std::fabs(yaw) < 0.01f)
            std::printf("  yaw %5.2f: moved (%.2f,%.2f) -> view-space x %+.3f\n",
                        yaw, moved.x, moved.z, sx);
        check(length(moved) > 0.05f, "the player actually moves when strafing");
        check(sx > 0.01f, "strafing right goes right on screen");
    }
    {
        Player p;
        p.reset(w.playerStart);
        p.yaw = 0.6f;
        vec3 before = p.pos;
        p.moveInput = vec2(-1.0f, 0.0f);
        for (int i = 0; i < 12; i++) p.update(dt, w, audio, 100.0f, false);
        float sx = viewSpaceX(before + vec3(0, 1.6f, 0), 0.6f, 0.0f,
                              p.pos + vec3(0, 1.6f, 0));
        std::printf("  stick left -> view-space x %+.3f\n", sx);
        check(sx < -0.01f, "strafing left goes left on screen");
    }

    // --- walking forward --------------------------------------------------
    std::printf("\n[forward] pushing the stick up must move you away from the camera\n");
    for (float yaw : yaws) {
        Player p;
        p.reset(w.playerStart);
        p.yaw = yaw;
        vec3 before = p.pos;
        p.moveInput = vec2(0.0f, 1.0f);
        for (int i = 0; i < 12; i++) p.update(dt, w, audio, 100.0f, false);
        vec3 moved = p.pos - before;
        vec3 f = p.forward();
        f.y = 0.0f;
        check(length(moved) > 0.05f, "the player moves when walking forward");
        check(dot(normalize(moved), normalize(f)) > 0.99f, "forward goes where you are facing");
        // And it must not drift sideways.
        check(std::fabs(dot(normalize(moved), p.right())) < 0.02f, "forward does not drift sideways");
    }

    // --- looking ----------------------------------------------------------
    std::printf("\n[look] dragging right must turn the view right\n");
    for (float yaw : yaws) {
        Player p;
        p.reset(w.playerStart);
        p.yaw = yaw;
        vec3 eye = p.eye();
        // A landmark straight ahead, before the turn.
        vec3 ahead = eye + p.forward() * 20.0f;

        // Exactly what the touch handler does for a rightward drag.
        p.applyLookDrag(60.0f, 0.0f, 1.0f);
        p.update(dt, w, audio, 100.0f, false);

        float sx = viewSpaceX(eye, p.yaw, p.pitch, ahead);
        if (std::fabs(yaw) < 0.01f)
            std::printf("  yaw %5.2f -> %5.2f: landmark ahead now at view-space x %+.3f\n",
                        yaw, p.yaw, sx);
        // Turning right means the world slides left past you.
        check(sx < -0.1f, "dragging right turns right, so a landmark ahead slides left");
    }
    {
        Player p;
        p.reset(w.playerStart);
        p.yaw = 1.1f;
        vec3 eye = p.eye();
        vec3 ahead = eye + p.forward() * 20.0f;
        p.applyLookDrag(-60.0f, 0.0f, 1.0f);
        p.update(dt, w, audio, 100.0f, false);
        float sx = viewSpaceX(eye, p.yaw, p.pitch, ahead);
        std::printf("  dragging left: landmark at view-space x %+.3f\n", sx);
        check(sx > 0.1f, "dragging left turns left");
    }

    std::printf("\n[look] dragging up must raise the view\n");
    {
        Player p;
        p.reset(w.playerStart);
        p.yaw = 0.4f;
        vec3 eye = p.eye();
        vec3 ahead = eye + p.forward() * 20.0f;
        p.applyLookDrag(0.0f, -60.0f, 1.0f);     // drag up: dy is negative
        p.update(dt, w, audio, 100.0f, false);
        float sy = viewSpaceY(eye, p.yaw, p.pitch, ahead);
        std::printf("  pitch now %+.3f, landmark at view-space y %+.3f\n", p.pitch, sy);
        check(p.pitch > 0.05f, "dragging up pitches the camera up");
        check(sy < -0.05f, "so a landmark ahead slides down the screen");
    }
    {
        // Pitch must stay clamped and must never roll the camera over.
        Player p;
        p.reset(w.playerStart);
        for (int i = 0; i < 400; i++) {
            p.applyLookDrag(0.0f, -80.0f, 1.0f);
            p.update(dt, w, audio, 100.0f, false);
        }
        std::printf("  after holding up for 400 frames, pitch = %+.3f\n", p.pitch);
        check(p.pitch < 1.5f && p.pitch > 1.3f, "pitch clamps just short of straight up");
        check(p.forward().y > 0.9f, "and the view is looking up, not flipped over");
    }

    // --- audio panning ----------------------------------------------------
    std::printf("\n[audio] a sound on your right must be heard on the right\n");
    {
        // The mixer computes its own right vector; it must agree with the
        // controller's, or footsteps pan the wrong way while you strafe.
        for (float yaw : yaws) {
            vec3 f = yawForward(yaw);
            vec3 mixerRight = normalize(cross(f, vec3(0, 1, 0)));
            Player p;
            p.reset(w.playerStart);
            p.yaw = yaw;
            check(dot(mixerRight, p.right()) > 0.999f,
                  "the mixer's right vector matches the controller's");
        }
    }

    // --- HUD --------------------------------------------------------------
    std::printf("\n[hud] the compass and minimap must point the right way\n");
    for (float yaw : yaws) {
        vec3 f = yawForward(yaw);
        vec3 r = yawRight(yaw);

        // Straight ahead.
        float bAhead = relativeBearing(f * 30.0f, yaw);
        vec2 mAhead = minimapOffset(f * 30.0f, yaw, 2.0f);
        check(std::fabs(bAhead) < 0.01f, "a target dead ahead reads as bearing zero");
        check(mAhead.y < -1.0f, "a target dead ahead appears above the minimap centre");
        check(std::fabs(mAhead.x) < 0.5f, "and not off to one side");

        // To the right.
        float bRight = relativeBearing(r * 30.0f, yaw);
        vec2 mRight = minimapOffset(r * 30.0f, yaw, 2.0f);
        check(bRight > 1.5f, "a target to your right reads as a positive bearing");
        check(mRight.x > 1.0f, "a target to your right appears right of the minimap centre");
        check(std::fabs(mRight.y) < 0.5f, "and level with it");

        // To the left, and behind.
        check(relativeBearing(r * -30.0f, yaw) < -1.5f, "a target to your left reads negative");
        check(minimapOffset(r * -30.0f, yaw, 2.0f).x < -1.0f, "and draws left of centre");
        check(minimapOffset(f * -30.0f, yaw, 2.0f).y > 1.0f, "a target behind draws below centre");

        // Distances must be preserved: the minimap is a rotation, not a shear.
        vec3 diag = normalize(f + r) * 25.0f;
        vec2 mDiag = minimapOffset(diag, yaw, 2.0f);
        float mapLen = std::sqrt(mDiag.x * mDiag.x + mDiag.y * mDiag.y);
        check(std::fabs(mapLen - 50.0f) < 0.5f, "the minimap preserves distance");
    }

    // --- the virtual stick ------------------------------------------------
    std::printf("\n[stick] dead zone, circular range, no diagonal speed bonus\n");
    {
        Player p;
        p.reset(w.playerStart);

        // A thumb resting on the glass must not move the player at all.
        p.applyStick(6.0f, 4.0f, 90.0f);
        std::printf("  tiny deflection -> input (%.3f, %.3f)\n", p.moveInput.x, p.moveInput.y);
        check(length(p.moveInput) < 0.001f, "a deflection inside the dead zone is ignored");

        vec3 before = p.pos;
        for (int i = 0; i < 30; i++) p.update(dt, w, audio, 100.0f, false);
        check(length(p.pos - before) < 0.001f, "and the player does not creep forward");

        // Full deflection reaches full magnitude, in every direction.
        p.applyStick(0.0f, -90.0f, 90.0f);
        check(std::fabs(length(p.moveInput) - 1.0f) < 0.02f, "full push forward is full speed");
        p.applyStick(90.0f, 0.0f, 90.0f);
        check(std::fabs(length(p.moveInput) - 1.0f) < 0.02f, "full push right is full speed");

        // A diagonal at the same physical deflection must not be faster.
        p.applyStick(64.0f, -64.0f, 90.0f);
        float diag = length(p.moveInput);
        std::printf("  diagonal at the rim -> magnitude %.3f\n", diag);
        check(diag < 1.02f, "a diagonal push is never faster than a straight one");
        check(diag > 0.9f, "but a diagonal at the rim is still near full speed");

        // Beyond the rim it must saturate rather than run away.
        p.applyStick(400.0f, 0.0f, 90.0f);
        check(std::fabs(length(p.moveInput) - 1.0f) < 0.02f, "past the rim saturates at full speed");

        // And the directions map correctly: up is forward, right is right.
        p.applyStick(0.0f, -90.0f, 90.0f);
        check(p.moveInput.y > 0.9f, "dragging the stick up means forward");
        p.applyStick(0.0f, 90.0f, 90.0f);
        check(p.moveInput.y < -0.9f, "dragging the stick down means backward");
        p.applyStick(90.0f, 0.0f, 90.0f);
        check(p.moveInput.x > 0.9f, "dragging the stick right means right");
        p.applyStick(-90.0f, 0.0f, 90.0f);
        check(p.moveInput.x < -0.9f, "dragging the stick left means left");
    }

    // Walking backward must actually go backward, not just not-forward.
    std::printf("\n[backward] pulling the stick down must reverse you\n");
    for (float yaw : yaws) {
        Player p;
        p.reset(w.playerStart);
        p.yaw = yaw;
        vec3 before = p.pos;
        p.applyStick(0.0f, 90.0f, 90.0f);
        for (int i = 0; i < 12; i++) p.update(dt, w, audio, 100.0f, false);
        vec3 moved = p.pos - before;
        vec3 f = p.forward();
        f.y = 0.0f;
        check(length(moved) > 0.03f, "the player moves when walking backward");
        check(dot(normalize(moved), normalize(f)) < -0.99f, "backward is opposite to facing");
    }

    // --- sensitivity and inversion ---------------------------------------
    std::printf("\n[options] look sensitivity and Y inversion\n");
    {
        Player p1, p2;
        p1.reset(w.playerStart);
        p2.reset(w.playerStart);
        p1.lookSensitivity = 1.0f;
        p2.lookSensitivity = 2.0f;
        p1.applyLookDrag(50.0f, 0.0f, 1.0f);
        p2.applyLookDrag(50.0f, 0.0f, 1.0f);
        p1.update(dt, w, audio, 100.0f, false);
        p2.update(dt, w, audio, 100.0f, false);
        float d1 = std::fabs(p1.yaw), d2 = std::fabs(p2.yaw);
        std::printf("  1.0x turned %.3f rad, 2.0x turned %.3f rad\n", d1, d2);
        check(d2 > d1 * 1.9f && d2 < d1 * 2.1f, "doubling sensitivity doubles the turn");

        Player p3;
        p3.reset(w.playerStart);
        p3.invertY = true;
        p3.applyLookDrag(0.0f, -60.0f, 1.0f);
        p3.update(dt, w, audio, 100.0f, false);
        std::printf("  inverted Y: dragging up gives pitch %+.3f\n", p3.pitch);
        check(p3.pitch < -0.05f, "with invert-Y on, dragging up looks down");
    }

    audio.stop();
    std::printf("\n=== %d checks, %d failures ===\n", gChecks, gFails);
    return gFails ? 1 : 0;
}
