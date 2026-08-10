// game.h - state machine, touch input, HUD and the scare director.
#pragma once
#include "gfx.h"
#include "world.h"
#include "player.h"
#include "entity.h"
#include "audio.h"
#include <string>

namespace hm {

enum GameState {
    GS_TITLE = 0,
    GS_INTRO,
    GS_PLAY,
    GS_DEAD,
    GS_CHAPTER_DONE,
    GS_SERIES_DONE
};

struct Touch {
    int id = -1;
    bool active = false;
    float startX = 0, startY = 0;
    float x = 0, y = 0;
    float downTime = 0.0f;
    int role = 0;          // 0 none, 1 move stick, 2 look, 3 button
    int button = -1;
    bool moved = false;
};

enum ButtonId { BTN_SPRINT = 0, BTN_CROUCH, BTN_TORCH, BTN_INTERACT, BTN_COUNT };

class Game {
public:
    bool init(const char* dataPath);
    void shutdown();
    void surfaceCreated();
    void surfaceChanged(int w, int h);
    void update(float dt);
    void render();

    void onTouchDown(int id, float x, float y);
    void onTouchMove(int id, float x, float y);
    void onTouchUp(int id, float x, float y);
    void onBackPressed();
    void onPause();
    void onResume();

    bool wantsQuit() const { return mWantQuit; }

    AudioEngine audio;

private:
    Renderer mR;
    World mWorld;
    Player mPlayer;
    Stalker mStalker;

    GpuMesh mWorldMesh, mPropMesh, mPickupMesh, mExitMesh;
    GpuMesh mStalkerParts[BP_COUNT];

    GameState mState = GS_TITLE;
    int mChapter = 0;
    int mUnlocked = 1;
    int mCollected = 0;
    float mTime = 0.0f;
    float mStateTime = 0.0f;
    float mFade = 1.0f;
    float mDamage = 0.0f;
    float mDeathTimer = 0.0f;
    bool  mExitActive = false;
    bool  mWantQuit = false;
    bool  mPaused = false;
    bool  mReady = false;

    // scare director
    float mAmbientTimer = 3.0f;
    float mWhisperTimer = 20.0f;
    float mLightPopTimer = 25.0f;
    float mNearMissCooldown = 0.0f;

    std::string mMessage;
    float mMessageTime = 0.0f;

    // input
    static const int MAX_TOUCH = 8;
    Touch mTouch[MAX_TOUCH];
    int mStickTouch = -1;
    int mLookTouch = -1;
    float mBtnX[BTN_COUNT], mBtnY[BTN_COUNT], mBtnR[BTN_COUNT];
    bool mBtnDown[BTN_COUNT] = {false, false, false, false};
    bool mInteractAvailable = false;

    // perf governor
    float mFpsAccum = 0.0f;
    int mFpsFrames = 0;
    float mFps = 60.0f;
    float mGovernorTimer = 4.0f;

    std::string mSavePath;
    float mUiScale = 1.0f;

    void loadProgress();
    void saveProgress();
    void startChapter(int index);
    void layoutButtons();
    void updatePlay(float dt);
    void updateDirector(float dt);
    void showMessage(const char* s, float seconds);
    void buildPropsMesh();
    void buildPickupMesh();
    void buildExitMesh();
    void gatherLights(SceneParams& sp);
    void renderHud();
    void renderTitle();
    void renderOverlayText();
    Touch* findTouch(int id);
};

} // namespace hm
