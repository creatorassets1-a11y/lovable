// JNI surface. Kotlin owns the window, the GL context and touch events; it
// forwards all of them here and the C++ side owns everything else.

#include <jni.h>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include <chrono>
#include <memory>

#include "audio/audio.h"
#include "core/asset.h"
#include "game/game.h"

using namespace hl;

namespace {

std::unique_ptr<Game> g_game;
std::unique_ptr<AudioEngine> g_audio;
Input g_input;
// Edge-triggered actions are latched by the UI thread and consumed by the
// render thread, so a tap can never be missed between frames.
bool g_pending_interact = false;
bool g_pending_torch = false;
bool g_pending_pause = false;
bool g_pending_tap = false;
std::chrono::steady_clock::time_point g_last;
bool g_have_last = false;

}  // namespace

extern "C" {

JNIEXPORT void JNICALL
Java_com_blackmoor_matron_Native_setAssetManager(JNIEnv* env, jclass, jobject mgr) {
    asset_set_android(AAssetManager_fromJava(env, mgr));
}

JNIEXPORT jboolean JNICALL
Java_com_blackmoor_matron_Native_onSurfaceCreated(JNIEnv*, jclass, jint w, jint h) {
    // A new GL context means every GPU object is gone; rebuild from scratch.
    g_game.reset();
    if (!g_audio) {
        g_audio = std::make_unique<AudioEngine>();
        g_audio->start();
    }
    g_game = std::make_unique<Game>();
    if (!g_game->init(w, h, g_audio.get())) {
        loge("game init failed");
        g_game.reset();
        return JNI_FALSE;
    }
    g_have_last = false;
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_blackmoor_matron_Native_onSurfaceChanged(JNIEnv*, jclass, jint w, jint h) {
    if (g_game) g_game->resize(w, h);
}

JNIEXPORT void JNICALL
Java_com_blackmoor_matron_Native_setInsets(JNIEnv*, jclass, jfloat l, jfloat t,
                                           jfloat r, jfloat b) {
    // Stored on the game so the HUD can keep clear of notches and gesture bars.
    if (g_game) g_game->set_insets(l, t, r, b);
}

JNIEXPORT void JNICALL
Java_com_blackmoor_matron_Native_setMove(JNIEnv*, jclass, jfloat x, jfloat y) {
    g_input.move = v2(x, y);
}

JNIEXPORT void JNICALL
Java_com_blackmoor_matron_Native_addLook(JNIEnv*, jclass, jfloat x, jfloat y) {
    g_input.look.x += x;
    g_input.look.y += y;
}

JNIEXPORT void JNICALL
Java_com_blackmoor_matron_Native_setButtons(JNIEnv*, jclass, jboolean run,
                                            jboolean crouch) {
    g_input.run = run;
    g_input.crouch = crouch;
}

JNIEXPORT void JNICALL
Java_com_blackmoor_matron_Native_press(JNIEnv*, jclass, jint which) {
    switch (which) {
        case 0: g_pending_interact = true; break;
        case 1: g_pending_torch = true; break;
        case 2: g_pending_pause = true; break;
        default: break;
    }
    g_pending_tap = true;
}

JNIEXPORT void JNICALL
Java_com_blackmoor_matron_Native_onDrawFrame(JNIEnv*, jclass) {
    if (!g_game) return;

    auto now = std::chrono::steady_clock::now();
    float dt = 1.0f / 60.0f;
    if (g_have_last) {
        dt = std::chrono::duration<float>(now - g_last).count();
    }
    g_last = now;
    g_have_last = true;

    g_input.interact = g_pending_interact;
    g_input.torch = g_pending_torch;
    g_input.pause = g_pending_pause;
    g_input.any_tap = g_pending_tap;
    g_pending_interact = g_pending_torch = g_pending_pause = g_pending_tap = false;

    static float clock = 0;
    clock += dt;

    g_game->update(dt, g_input);
    g_game->render(clock);

    // Look is a per-frame delta; clear it once consumed.
    g_input.look = v2(0, 0);

    auto end = std::chrono::steady_clock::now();
    g_game->report_frame_ms(std::chrono::duration<float, std::milli>(end - now).count());
}

JNIEXPORT void JNICALL
Java_com_blackmoor_matron_Native_onPause(JNIEnv*, jclass) {
    if (g_game) g_game->on_pause();
}

JNIEXPORT void JNICALL
Java_com_blackmoor_matron_Native_onResume(JNIEnv*, jclass) {
    if (g_game) g_game->on_resume();
}

JNIEXPORT void JNICALL
Java_com_blackmoor_matron_Native_onDestroy(JNIEnv*, jclass) {
    if (g_game) {
        g_game->shutdown();
        g_game.reset();
    }
    if (g_audio) {
        g_audio->stop();
        g_audio.reset();
    }
}

JNIEXPORT jint JNICALL
Java_com_blackmoor_matron_Native_consumeHaptic(JNIEnv*, jclass) {
    return (jint)pop_haptic();
}

JNIEXPORT jint JNICALL
Java_com_blackmoor_matron_Native_getPhase(JNIEnv*, jclass) {
    return g_game ? (jint)g_game->phase() : 0;
}

}  // extern "C"
