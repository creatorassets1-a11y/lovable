// main.cpp - NativeActivity entry point: EGL, lifecycle, input pump.
#include "game.h"
#include <android_native_app_glue.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <ctime>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HollowSignal", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "HollowSignal", __VA_ARGS__)

#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x0040
#endif

namespace {

struct Engine {
    android_app* app = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int width = 0, height = 0;
    bool hasGl = false;
    bool focused = false;
    bool inited = false;
    double lastTime = 0.0;
    hm::Game game;
};

Engine gEngine;

double nowSeconds() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

bool initDisplay(Engine* e) {
    if (e->hasGl) return true;

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) { LOGE("eglGetDisplay failed"); return false; }
    if (!eglInitialize(dpy, nullptr, nullptr)) { LOGE("eglInitialize failed"); return false; }

    // Ask for an ES3-capable config first. If the driver will not advertise
    // ES3 in RENDERABLE_TYPE we retry with the ES2 bit and still request an
    // ES3 context - some older drivers report the bit incorrectly.
    const EGLint attribsES3[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };
    const EGLint attribsES2[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };

    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(dpy, attribsES3, &config, 1, &numConfigs) || numConfigs < 1) {
        if (!eglChooseConfig(dpy, attribsES2, &config, 1, &numConfigs) || numConfigs < 1) {
            LOGE("no suitable EGL config");
            return false;
        }
    }

    EGLint format = 0;
    eglGetConfigAttrib(dpy, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(e->app->window, 0, 0, format);

    EGLSurface surf = eglCreateWindowSurface(dpy, config, e->app->window, nullptr);
    if (surf == EGL_NO_SURFACE) { LOGE("eglCreateWindowSurface failed"); return false; }

    const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctxAttribs);
    if (ctx == EGL_NO_CONTEXT) { LOGE("eglCreateContext(ES3) failed"); return false; }

    if (!eglMakeCurrent(dpy, surf, surf, ctx)) { LOGE("eglMakeCurrent failed"); return false; }

    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);

    e->display = dpy;
    e->surface = surf;
    e->context = ctx;
    e->width = w;
    e->height = h;
    e->hasGl = true;

    // Do not tear; a horror game gains nothing from an unlocked framerate and
    // loses battery on every device.
    eglSwapInterval(dpy, 1);

    e->game.surfaceCreated();
    e->game.surfaceChanged(w, h);
    LOGI("EGL up: %dx%d, GL_VERSION=%s", w, h, (const char*)glGetString(GL_VERSION));
    return true;
}

void termDisplay(Engine* e) {
    if (e->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(e->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (e->context != EGL_NO_CONTEXT) eglDestroyContext(e->display, e->context);
        if (e->surface != EGL_NO_SURFACE) eglDestroySurface(e->display, e->surface);
        eglTerminate(e->display);
    }
    e->display = EGL_NO_DISPLAY;
    e->context = EGL_NO_CONTEXT;
    e->surface = EGL_NO_SURFACE;
    e->hasGl = false;
}

void drawFrame(Engine* e) {
    if (!e->hasGl) return;

    double t = nowSeconds();
    float dt = (float)(t - e->lastTime);
    e->lastTime = t;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;

    // Surfaces can be resized under us (rotation, split screen, cutout insets).
    EGLint w = 0, h = 0;
    eglQuerySurface(e->display, e->surface, EGL_WIDTH, &w);
    eglQuerySurface(e->display, e->surface, EGL_HEIGHT, &h);
    if (w > 0 && h > 0 && (w != e->width || h != e->height)) {
        e->width = w;
        e->height = h;
        e->game.surfaceChanged(w, h);
    }

    e->game.update(dt);
    e->game.render();
    eglSwapBuffers(e->display, e->surface);
}

int32_t handleInput(android_app* app, AInputEvent* event) {
    Engine* e = (Engine*)app->userData;
    int32_t type = AInputEvent_getType(event);

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event);
        int32_t flags = action & AMOTION_EVENT_ACTION_MASK;
        size_t index = (size_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        switch (flags) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN: {
            int id = AMotionEvent_getPointerId(event, index);
            e->game.onTouchDown(id, AMotionEvent_getX(event, index), AMotionEvent_getY(event, index));
            return 1;
        }
        case AMOTION_EVENT_ACTION_MOVE: {
            size_t count = AMotionEvent_getPointerCount(event);
            for (size_t i = 0; i < count; i++) {
                int id = AMotionEvent_getPointerId(event, i);
                e->game.onTouchMove(id, AMotionEvent_getX(event, i), AMotionEvent_getY(event, i));
            }
            return 1;
        }
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP: {
            int id = AMotionEvent_getPointerId(event, index);
            e->game.onTouchUp(id, AMotionEvent_getX(event, index), AMotionEvent_getY(event, index));
            return 1;
        }
        case AMOTION_EVENT_ACTION_CANCEL: {
            size_t count = AMotionEvent_getPointerCount(event);
            for (size_t i = 0; i < count; i++) {
                int id = AMotionEvent_getPointerId(event, i);
                e->game.onTouchUp(id, AMotionEvent_getX(event, i), AMotionEvent_getY(event, i));
            }
            return 1;
        }
        default:
            return 0;
        }
    } else if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t code = AKeyEvent_getKeyCode(event);
        if (code == AKEYCODE_BACK) {
            if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP) {
                e->game.onBackPressed();
                if (e->game.wantsQuit()) ANativeActivity_finish(app->activity);
            }
            return 1;
        }
    }
    return 0;
}

void handleCmd(android_app* app, int32_t cmd) {
    Engine* e = (Engine*)app->userData;
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window) {
            if (!e->inited) {
                e->game.init(app->activity->internalDataPath);
                e->inited = true;
            }
            initDisplay(e);
            e->lastTime = nowSeconds();
            drawFrame(e);
        }
        break;
    case APP_CMD_TERM_WINDOW:
        termDisplay(e);
        break;
    case APP_CMD_GAINED_FOCUS:
        e->focused = true;
        e->lastTime = nowSeconds();
        e->game.onResume();
        break;
    case APP_CMD_LOST_FOCUS:
        e->focused = false;
        e->game.onPause();
        break;
    case APP_CMD_PAUSE:
        e->game.onPause();
        break;
    case APP_CMD_RESUME:
        e->lastTime = nowSeconds();
        e->game.onResume();
        break;
    case APP_CMD_SAVE_STATE:
        break;
    case APP_CMD_DESTROY:
        e->game.shutdown();
        break;
    default:
        break;
    }
}

} // namespace

void android_main(android_app* app) {
    gEngine.app = app;
    app->userData = &gEngine;
    app->onAppCmd = handleCmd;
    app->onInputEvent = handleInput;
    gEngine.lastTime = nowSeconds();

    while (true) {
        int events;
        android_poll_source* source;
        // Block only when we have nothing to draw; otherwise poll and render.
        int timeout = (gEngine.focused && gEngine.hasGl) ? 0 : -1;
        while (ALooper_pollOnce(timeout, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) {
                gEngine.game.shutdown();
                termDisplay(&gEngine);
                return;
            }
            timeout = 0;   // drain anything else already queued, then draw
        }
        if (gEngine.focused && gEngine.hasGl) drawFrame(&gEngine);
    }
}
