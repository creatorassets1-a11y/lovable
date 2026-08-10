// game.h - state machine, open-world streaming, HUD and the scare director.
#pragma once
#include "gfx.h"
#include "world.h"
#include "player.h"
#include "actors.h"
#include "missions.h"
#include "audio.h"
#include "assets.h"
#include <string>
#include <vector>

struct AAssetManager;

namespace hm {

enum GameState {
    GS_LOADING = 0,
    GS_TITLE,
    GS_BRIEF,
    GS_PLAY,
    GS_DEAD,
    GS_MISSION_DONE,
    GS_GAME_DONE
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

enum ButtonId { BTN_SPRINT = 0, BTN_CROUCH, BTN_TORCH, BTN_FLARE, BTN_INTERACT, BTN_COUNT };

const int MAX_MONSTERS = 8;
const int MAX_NPCS = 6;

class Game {
public:
    bool init(const char* dataPath, AAssetManager* assetMgr);
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

    // Queried by the Kotlin layer for the notification shade / save summary.
    int missionIndex() const { return mMission.index; }
    int missionsUnlocked() const { return mUnlocked; }

    AudioEngine audio;

private:
    Renderer mR;
    World mWorld;
    Player mPlayer;
    AssetPack mPack;

    std::vector<Chunk> mChunks;
    std::vector<GpuMesh> mChunkMeshes;
    GpuMesh mPropMesh;
    GpuMesh mPickupMesh, mDeviceMesh, mMarkerMesh;
    GpuMesh mActorParts[AK_KIND_COUNT][BP_COUNT];

    Monster mMonsters[MAX_MONSTERS];
    Npc mNpcs[MAX_NPCS];
    MissionState mMission;

    GameState mState = GS_LOADING;
    int  mUnlocked = 1;
    float mTime = 0.0f;
    float mStateTime = 0.0f;
    float mFade = 1.0f;
    float mDamage = 0.0f;
    bool  mWantQuit = false;
    bool  mPaused = false;
    bool  mReady = false;
    bool  mPackOk = false;

    float mBrightness = 1.15f;   // player-set exposure, persisted
    float mSensitivity = 1.0f;   // look sensitivity, persisted
    bool  mInvertY = false;      // persisted
    bool  mOnDoorway = false;    // edge-detects crossing a threshold
    int   mFlares = 3;
    float mFlareCooldown = 0.0f;

    // scare director
    float mAmbientTimer = 3.0f;
    float mWhisperTimer = 20.0f;
    float mLightPopTimer = 25.0f;
    float mNearMissCooldown = 0.0f;
    float mRadioTimer = 30.0f;

    // cached sample handles
    int mBedStreet = -1, mBedInterior = -1, mBedTunnels = -1;
    int mVoBrief = -1;

    std::string mMessage;
    float mMessageTime = 0.0f;
    std::string mSubtitle;
    float mSubtitleTime = 0.0f;

    static const int MAX_TOUCH = 8;
    Touch mTouch[MAX_TOUCH];
    int mStickTouch = -1;
    int mLookTouch = -1;
    float mBtnX[BTN_COUNT], mBtnY[BTN_COUNT], mBtnR[BTN_COUNT];
    bool mBtnDown[BTN_COUNT] = {false, false, false, false, false};
    bool mInteractAvailable = false;
    int  mInteractTarget = -1;

    float mFpsAccum = 0.0f;
    int mFpsFrames = 0;
    float mFps = 60.0f;
    float mGovernorTimer = 4.0f;

    std::string mSavePath;
    float mUiScale = 1.0f;
    AAssetManager* mAssetMgr = nullptr;

    void loadProgress();
    void saveProgress();
    void startNewGame();
    void startMission(int index);
    void completeMission();
    void spawnMonstersFor(float pressure);
    void layoutButtons();
    void updatePlay(float dt);
    void updateDirector(float dt);
    void updateMission(float dt);
    void showMessage(const char* s, float seconds);
    void say(const char* voiceName, const char* subtitle, float seconds);
    void buildPropsMesh();
    void buildSmallMeshes();
    void uploadWorld();
    void gatherLights(SceneParams& sp);
    void renderWorld(const SceneParams& sp, bool shadowPass);
    void renderHud();
    // Geometry of the title screen, computed once and used by both the drawing
    // and the hit-testing so a control can never be drawn somewhere you cannot
    // press.
    struct TitleLayout {
        float startX, startY, startR;
        float rowY[3];          // brightness, sensitivity, invert-Y
        float minusX, plusX;
        float rowR;
    };
    TitleLayout titleLayout() const;
    void renderTitle();
    void renderOverlayText();
    void renderCompass();
    void renderMiniMap();
    Touch* findTouch(int id);
    float nearestMonsterDist(vec3& outPos) const;
};

} // namespace hm
