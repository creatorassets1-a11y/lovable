// jnibridge.cpp - the JNI edge between the C++ engine and the Kotlin/Java shell.
//
// Two directions cross here. The Kotlin activity asks the engine for progress
// so it can mirror it into SharedPreferences, and the engine asks the Java
// helpers for haptics and for a device tier. Both directions fail soft: if the
// VM or a class is missing, the calls do nothing rather than crash.
#include "platform.h"
#include "game.h"
#include <jni.h>
#include <android/log.h>

#define JLOG(...) __android_log_print(ANDROID_LOG_INFO, "HollowJNI", __VA_ARGS__)
#define JERR(...) __android_log_print(ANDROID_LOG_WARN, "HollowJNI", __VA_ARGS__)

namespace {

JavaVM* gVm = nullptr;
hm::Game* gGame = nullptr;

// Cached as globals so the lookups happen once rather than per scare.
jclass gHapticsClass = nullptr;
jmethodID gHapticsPlay = nullptr;
jclass gProfileClass = nullptr;
jmethodID gProfileTier = nullptr;
int gCachedTier = -1;

// Attaches the calling thread if needed. The audio thread never calls in here,
// but the game thread is the native-activity thread and is already attached;
// this is belt and braces for the case where that changes.
struct ScopedEnv {
    JNIEnv* env = nullptr;
    bool attached = false;

    ScopedEnv() {
        if (!gVm) return;
        if (gVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) return;
        if (gVm->AttachCurrentThread(&env, nullptr) == JNI_OK) attached = true;
        else env = nullptr;
    }
    ~ScopedEnv() {
        if (attached && gVm) gVm->DetachCurrentThread();
    }
    bool ok() const { return env != nullptr; }
};

bool resolveClasses(JNIEnv* env) {
    if (gHapticsClass && gProfileClass) return true;

    jclass h = env->FindClass("com/hollowsignal/game/Haptics");
    if (h) {
        gHapticsClass = static_cast<jclass>(env->NewGlobalRef(h));
        gHapticsPlay = env->GetStaticMethodID(gHapticsClass, "play", "(I)V");
        env->DeleteLocalRef(h);
    } else {
        env->ExceptionClear();
        JERR("Haptics class not found; vibration disabled");
    }

    jclass p = env->FindClass("com/hollowsignal/game/DeviceProfile");
    if (p) {
        gProfileClass = static_cast<jclass>(env->NewGlobalRef(p));
        gProfileTier = env->GetStaticMethodID(gProfileClass, "tier", "()I");
        env->DeleteLocalRef(p);
    } else {
        env->ExceptionClear();
        JERR("DeviceProfile class not found; assuming mid tier");
    }
    return gHapticsClass != nullptr || gProfileClass != nullptr;
}

} // namespace

namespace hm {

void platformSetGame(Game* g) { gGame = g; }

void platformHaptic(HapticId id) {
    ScopedEnv e;
    if (!e.ok()) return;
    if (!resolveClasses(e.env)) return;
    if (!gHapticsClass || !gHapticsPlay) return;
    e.env->CallStaticVoidMethod(gHapticsClass, gHapticsPlay, (jint)id);
    if (e.env->ExceptionCheck()) e.env->ExceptionClear();
}

int platformDeviceTier() {
    if (gCachedTier >= 0) return gCachedTier;
    ScopedEnv e;
    if (!e.ok()) return TIER_MID;
    if (!resolveClasses(e.env) || !gProfileClass || !gProfileTier) return TIER_MID;
    jint t = e.env->CallStaticIntMethod(gProfileClass, gProfileTier);
    if (e.env->ExceptionCheck()) {
        e.env->ExceptionClear();
        return TIER_MID;
    }
    gCachedTier = (int)t;
    JLOG("device tier %d", gCachedTier);
    return gCachedTier;
}

} // namespace hm

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    gVm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT jint JNICALL
Java_com_hollowsignal_game_MainActivity_nativeMissionIndex(JNIEnv*, jobject) {
    // -1 tells the Kotlin side "no answer yet", so it does not overwrite a
    // good saved value with a placeholder.
    return gGame ? (jint)gGame->missionIndex() : (jint)-1;
}

JNIEXPORT jint JNICALL
Java_com_hollowsignal_game_MainActivity_nativeMissionsUnlocked(JNIEnv*, jobject) {
    return gGame ? (jint)gGame->missionsUnlocked() : (jint)-1;
}

} // extern "C"
