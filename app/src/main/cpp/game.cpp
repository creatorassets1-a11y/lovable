#include "game.h"
#include "platform.h"
#include "noise.h"
#include "matids.h"
#include <android/log.h>
#include <cstdio>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HollowGame", __VA_ARGS__)

namespace hm {

static Rng gRng(0xC0FFEEu);

// 320m square district at 2m cells, chunked at 24 cells (48m) for culling.
static const int CITY_W = 160;
static const int CITY_H = 160;
static const int CHUNK_CELLS = 24;
static const uint32_t CITY_SEED = 0x4A17E5u;

// ------------------------------------------------------------------ lifecycle

bool Game::init(const char* dataPath, AAssetManager* assetMgr) {
    mSavePath = std::string(dataPath ? dataPath : ".") + "/progress.dat";
    mAssetMgr = assetMgr;
    loadProgress();

#if defined(__ANDROID__)
    mPackOk = mPack.openAndroid(assetMgr, "hollow.pak");
#endif
    if (mPackOk) audio.setPack(&mPack);
    audio.start();
    audio.setMasterGain(1.0f);

    mBedStreet = audio.sampleIndex("amb_street");
    mBedInterior = audio.sampleIndex("amb_interior");
    mBedTunnels = audio.sampleIndex("amb_tunnels");

    mWorld.generate(CITY_SEED, CITY_W, CITY_H);
    mWorld.buildChunks(mChunks, CHUNK_CELLS);
    LOGI("city: %dx%d cells, %d buildings, %d chunks, %d lights, %d props",
         mWorld.W, mWorld.H, (int)mWorld.buildings.size(), (int)mChunks.size(),
         (int)mWorld.lights.size(), (int)mWorld.props.size());

    // Start at a resolution the device can plausibly hold. The framerate
    // governor still adapts, but only after the player has already seen a few
    // bad seconds - beginning at the right tier avoids that entirely.
    switch (platformDeviceTier()) {
    case TIER_LOW:  mR.setRenderScale(0.60f); break;
    case TIER_HIGH: mR.setRenderScale(1.00f); break;
    default:        mR.setRenderScale(0.82f); break;
    }

    mState = GS_TITLE;
    mStateTime = 0.0f;
    mFade = 1.0f;
    return true;
}

void Game::shutdown() {
    saveProgress();
    audio.stop();
    for (GpuMesh& m : mChunkMeshes) m.destroy();
    mChunkMeshes.clear();
    mPropMesh.destroy();
    mPickupMesh.destroy();
    mDeviceMesh.destroy();
    mMarkerMesh.destroy();
    for (int k = 0; k < AK_KIND_COUNT; k++)
        for (int i = 0; i < BP_COUNT; i++) mActorParts[k][i].destroy();
    mR.shutdown();
    mPack.close();
    mReady = false;
}

void Game::loadProgress() {
    mUnlocked = 1;
    FILE* f = std::fopen(mSavePath.c_str(), "rb");
    if (!f) return;
    int v = 0, u = 1;
    if (std::fread(&v, sizeof(int), 1, f) == 1 && v == 2) {
        if (std::fread(&u, sizeof(int), 1, f) == 1)
            mUnlocked = (int)clampf((float)u, 1.0f, (float)MISSION_COUNT);
    }
    std::fclose(f);
}

void Game::saveProgress() {
    FILE* f = std::fopen(mSavePath.c_str(), "wb");
    if (!f) return;
    int v = 2;
    std::fwrite(&v, sizeof(int), 1, f);
    std::fwrite(&mUnlocked, sizeof(int), 1, f);
    std::fclose(f);
}

void Game::surfaceCreated() {
    // The GL context dies whenever the window goes away, taking every GL object
    // with it. Drop the stale handles before re-uploading: upload() would
    // otherwise see a non-zero VAO name and reuse one that no longer exists.
    for (GpuMesh& m : mChunkMeshes) m.orphan();
    mPropMesh.orphan();
    mPickupMesh.orphan();
    mDeviceMesh.orphan();
    mMarkerMesh.orphan();
    for (int k = 0; k < AK_KIND_COUNT; k++)
        for (int i = 0; i < BP_COUNT; i++) mActorParts[k][i].orphan();
    mReady = false;

    if (!mR.init(mPackOk ? &mPack : nullptr)) {
        LOGI("renderer init failed");
        return;
    }
    for (int k = 0; k < AK_KIND_COUNT; k++) {
        Mesh parts[BP_COUNT];
        buildActorParts((ActorKind)k, parts);
        for (int i = 0; i < BP_COUNT; i++) mActorParts[k][i].upload(parts[i]);
    }
    buildSmallMeshes();
    uploadWorld();
    mReady = true;
}

void Game::uploadWorld() {
    mChunkMeshes.resize(mChunks.size());
    size_t tris = 0;
    for (size_t i = 0; i < mChunks.size(); i++) {
        mChunkMeshes[i].upload(mChunks[i].mesh);
        tris += mChunks[i].mesh.triCount();
    }
    buildPropsMesh();
    LOGI("uploaded %d chunks, %d triangles", (int)mChunks.size(), (int)tris);
}

void Game::surfaceChanged(int w, int h) {
    mR.resize(w, h);
    mUiScale = std::min((float)w, (float)h) / 720.0f;
    layoutButtons();
}

void Game::onPause() {
    mPaused = true;
    audio.setMasterGain(0.0f);
    saveProgress();
}

void Game::onResume() {
    mPaused = false;
    audio.setMasterGain(1.0f);
}

void Game::onBackPressed() {
    if (mState == GS_PLAY || mState == GS_BRIEF) {
        mState = GS_TITLE;
        mStateTime = 0.0f;
        mFade = 1.0f;
        audio.setTension(0.0f);
        audio.setBreath(0.0f);
        audio.setMuffle(0.0f);
    } else {
        mWantQuit = true;
    }
}

// ------------------------------------------------------------------- meshes

void Game::buildPropsMesh() {
    Mesh out;
    Mesh lamp, car, bin, barrier, pallet, rubble;
    Mesh desk, chair, cabinet, crate;

    lamp.taperedPrism({0, 0, 0}, 5.2f, 0.11f, 0.07f, 8, (float)MAT_RUSTMETAL);
    lamp.box({-0.06f, 5.05f, -0.06f}, {0.75f, 5.18f, 0.06f}, (float)MAT_RUSTMETAL, 2.0f);
    lamp.box({0.52f, 4.86f, -0.17f}, {0.92f, 5.06f, 0.17f}, (float)MAT_CORRUGATED, 3.0f);

    // Wrecked car. It blocks sight lines at crouch height, which makes it
    // cover rather than scenery.
    car.box({-0.86f, 0.28f, -2.05f}, {0.86f, 0.92f, 2.05f}, (float)MAT_CARPAINT, 1.1f);
    car.box({-0.78f, 0.92f, -0.95f}, {0.78f, 1.42f, 0.85f}, (float)MAT_WINDOW, 1.4f);
    for (int i = 0; i < 4; i++) {
        float sx = (i & 1) ? 0.80f : -0.80f;
        float sz = (i & 2) ? 1.42f : -1.42f;
        car.taperedPrism({sx, 0.05f, sz}, 0.52f, 0.31f, 0.31f, 8, (float)MAT_RUSTMETAL);
    }
    bin.taperedPrism({0, 0, 0}, 1.05f, 0.35f, 0.32f, 10, (float)MAT_RUSTMETAL);
    bin.box({-0.36f, 1.05f, -0.36f}, {0.36f, 1.12f, 0.36f}, (float)MAT_RUSTMETAL, 2.0f);

    barrier.box({-0.9f, 0.0f, -0.06f}, {0.9f, 0.09f, 0.06f}, (float)MAT_CORRUGATED, 2.0f);
    barrier.box({-0.9f, 0.78f, -0.06f}, {0.9f, 0.95f, 0.06f}, (float)MAT_CORRUGATED, 2.0f);
    for (int i = 0; i < 2; i++)
        barrier.taperedPrism({i ? 0.78f : -0.78f, 0.0f, 0.0f}, 0.95f, 0.05f, 0.05f, 6,
                             (float)MAT_RUSTMETAL);

    pallet.box({-0.6f, 0.0f, -0.5f}, {0.6f, 0.14f, 0.5f}, (float)MAT_WOOD, 2.2f);
    pallet.box({-0.55f, 0.14f, -0.45f}, {0.55f, 0.62f, 0.45f}, (float)MAT_WOOD, 1.8f);
    for (int i = 0; i < 5; i++) {
        float a = (float)i * 1.7f;
        rubble.box({-0.35f + std::cos(a) * 0.3f, 0.0f, -0.35f + std::sin(a) * 0.3f},
                   {0.05f + std::cos(a) * 0.3f, 0.16f + (i % 3) * 0.09f, 0.05f + std::sin(a) * 0.3f},
                   (float)MAT_GRAVEL, 2.4f);
    }

    desk.box({-0.75f, 0.68f, -0.42f}, {0.75f, 0.78f, 0.42f}, (float)MAT_WOOD, 1.4f);
    for (int i = 0; i < 4; i++) {
        float sx = (i & 1) ? 0.66f : -0.66f;
        float sz = (i & 2) ? 0.34f : -0.34f;
        desk.box({sx - 0.04f, 0.0f, sz - 0.04f}, {sx + 0.04f, 0.68f, sz + 0.04f},
                 (float)MAT_RUSTMETAL, 3.0f);
    }
    chair.box({-0.22f, 0.42f, -0.22f}, {0.22f, 0.50f, 0.22f}, (float)MAT_CLOTH, 2.2f);
    chair.box({-0.22f, 0.50f, 0.14f}, {0.22f, 1.02f, 0.22f}, (float)MAT_CLOTH, 2.2f);
    for (int i = 0; i < 4; i++) {
        float sx = (i & 1) ? 0.18f : -0.18f;
        float sz = (i & 2) ? 0.18f : -0.18f;
        chair.box({sx - 0.025f, 0.0f, sz - 0.025f}, {sx + 0.025f, 0.42f, sz + 0.025f},
                  (float)MAT_RUSTMETAL, 3.0f);
    }
    cabinet.box({-0.42f, 0.0f, -0.26f}, {0.42f, 1.72f, 0.26f}, (float)MAT_RUSTMETAL, 1.3f);
    crate.box({-0.32f, 0.0f, -0.32f}, {0.32f, 0.60f, 0.32f}, (float)MAT_WOOD, 1.7f);

    const Mesh* kinds[10] = {&lamp, &car, &bin, &barrier, &pallet, &rubble,
                             &desk, &chair, &cabinet, &crate};
    for (const Prop& p : mWorld.props) {
        int k = p.kind;
        if (k < 0 || k > 9) k = 5;
        out.append(*kinds[k], mat4::translation(p.pos) * mat4::rotationY(p.yaw));
    }
    mPropMesh.upload(out);
    LOGI("props: %d objects, %d triangles", (int)mWorld.props.size(), (int)out.triCount());
}

void Game::buildSmallMeshes() {
    // Supply cache: a clean canister. It should be the only tidy object in
    // any room it is in.
    Mesh cache;
    cache.taperedPrism({0, -0.12f, 0}, 0.26f, 0.075f, 0.075f, 8, (float)MAT_CORRUGATED);
    cache.ellipsoid({0, 0.17f, 0}, {0.06f, 0.045f, 0.06f}, 10, 6, (float)MAT_RUSTMETAL);
    mPickupMesh.upload(cache);

    Mesh dev;
    dev.box({-0.28f, -0.34f, -0.14f}, {0.28f, 0.34f, 0.14f}, (float)MAT_RUSTMETAL, 1.6f);
    dev.box({-0.07f, -0.02f, 0.14f}, {0.07f, 0.26f, 0.20f}, (float)MAT_CARPAINT, 3.0f);
    mDeviceMesh.upload(dev);

    // Objective marker: a slim vertical beam. Deliberately not a floating
    // arrow - it reads as a light in the world rather than an overlay on it.
    Mesh marker;
    marker.taperedPrism({0, 0.0f, 0}, 3.4f, 0.055f, 0.012f, 6, (float)MAT_CORRUGATED);
    mMarkerMesh.upload(marker);
}

// ------------------------------------------------------------------ missions

void Game::startNewGame() {
    mPlayer.reset(mWorld.playerStart);
    mFlares = 3;
    mDamage = 0.0f;
    for (int i = 0; i < MAX_NPCS; i++) mNpcs[i].active = false;
    for (int i = 0; i < MAX_MONSTERS; i++) mMonsters[i].active = false;
    startMission(0);
}

void Game::spawnMonstersFor(float pressure) {
    for (int i = 0; i < MAX_MONSTERS; i++) mMonsters[i].active = false;

    // The mix escalates: a stalker from the start, crawlers once the pressure
    // is up, and watchers last, because they make everything else worse.
    int stalkers = 1 + (int)(pressure * 2.0f);
    int crawlers = (pressure > 0.35f) ? (int)((pressure - 0.35f) * 4.0f) + 1 : 0;
    int watchers = (pressure > 0.55f) ? (int)((pressure - 0.55f) * 3.0f) + 1 : 0;

    int slot = 0;
    auto spawnOne = [&](ActorKind k, float minDist) {
        if (slot >= MAX_MONSTERS) return;
        for (int tries = 0; tries < 300; tries++) {
            int x = gRng.rangei(1, mWorld.W - 1);
            int z = gRng.rangei(1, mWorld.H - 1);
            if (mWorld.solid(x, z)) continue;
            vec3 c = mWorld.cellCenter(x, z);
            if (length(c - mPlayer.pos) < minDist) continue;
            mMonsters[slot].spawn(mWorld, c, k, gRng.next());
            slot++;
            return;
        }
    };
    for (int i = 0; i < stalkers; i++) spawnOne(AK_STALKER, 55.0f);
    for (int i = 0; i < crawlers; i++) spawnOne(AK_CRAWLER, 45.0f);
    for (int i = 0; i < watchers; i++) spawnOne(AK_WATCHER, 35.0f);
    LOGI("monsters: %d stalkers, %d crawlers, %d watchers (pressure %.2f)",
         stalkers, crawlers, watchers, pressure);
}

void Game::startMission(int index) {
    Rng rng(0x9E11u + index * 7717u);
    buildMission(mMission, index, mWorld, mPlayer.pos, rng);
    const MissionDef& d = mMission.def();
    spawnMonstersFor(d.monsterPressure);

    for (int i = 0; i < MAX_NPCS; i++) mNpcs[i].active = false;
    if (d.type == MT_RESCUE) {
        for (size_t i = 0; i < mMission.objectives.size() && i < MAX_NPCS; i++) {
            mNpcs[i].spawn(mMission.objectives[i].pos, (int)i, gRng.next());
            mMission.objectives[i].npcId = (int)i;
        }
    }

    mState = GS_BRIEF;
    mStateTime = 0.0f;
    mFade = 1.0f;
    say(d.briefVoice, d.objective, 5.0f);
    LOGI("mission %d '%s': %s", index + 1, d.title, d.objective);
}

void Game::completeMission() {
    mState = GS_MISSION_DONE;
    mStateTime = 0.0f;
    mMission.complete = true;
    mMission.active = false;
    if (mMission.index + 1 > mUnlocked) {
        mUnlocked = std::min(MISSION_COUNT, mMission.index + 2);
        saveProgress();
    }
    audio.postUI(SND_RADIO_BEEP, 0.7f, 1.0f);
    say("vo_objective_done", "OBJECTIVE COMPLETE", 4.0f);
}

void Game::showMessage(const char* s, float seconds) {
    mMessage = s;
    mMessageTime = seconds;
}

void Game::say(const char* voiceName, const char* subtitle, float seconds) {
    int idx = audio.sampleIndex(voiceName);
    if (idx >= 0) audio.postVoice(idx, 0.95f);
    mSubtitle = subtitle ? subtitle : "";
    mSubtitleTime = seconds;
}

// --------------------------------------------------------------------- input

Touch* Game::findTouch(int id) {
    for (int i = 0; i < MAX_TOUCH; i++)
        if (mTouch[i].active && mTouch[i].id == id) return &mTouch[i];
    return nullptr;
}

void Game::layoutButtons() {
    float s = mUiScale;
    float w = (float)mR.width(), h = (float)mR.height();
    mBtnX[BTN_SPRINT] = w - 118.0f * s; mBtnY[BTN_SPRINT] = h - 118.0f * s; mBtnR[BTN_SPRINT] = 64.0f * s;
    mBtnX[BTN_CROUCH] = w - 252.0f * s; mBtnY[BTN_CROUCH] = h - 92.0f * s;  mBtnR[BTN_CROUCH] = 50.0f * s;
    mBtnX[BTN_TORCH]  = w - 108.0f * s; mBtnY[BTN_TORCH]  = h - 262.0f * s; mBtnR[BTN_TORCH]  = 50.0f * s;
    mBtnX[BTN_FLARE]  = w - 238.0f * s; mBtnY[BTN_FLARE]  = h - 230.0f * s; mBtnR[BTN_FLARE]  = 46.0f * s;
    mBtnX[BTN_INTERACT] = w * 0.5f;     mBtnY[BTN_INTERACT] = h - 132.0f * s; mBtnR[BTN_INTERACT] = 62.0f * s;
}

void Game::onTouchDown(int id, float x, float y) {
    int slot = -1;
    for (int i = 0; i < MAX_TOUCH; i++) if (!mTouch[i].active) { slot = i; break; }
    if (slot < 0) return;
    Touch& t = mTouch[slot];
    t.active = true; t.id = id;
    t.startX = t.x = x; t.startY = t.y = y;
    t.downTime = mTime; t.moved = false;
    t.role = 0; t.button = -1;

    if (mState != GS_PLAY) return;

    for (int b = 0; b < BTN_COUNT; b++) {
        if (b == BTN_INTERACT && !mInteractAvailable) continue;
        if (b == BTN_FLARE && mFlares <= 0) continue;
        float dx = x - mBtnX[b], dy = y - mBtnY[b];
        if (dx * dx + dy * dy <= mBtnR[b] * mBtnR[b] * 1.35f) {
            t.role = 3; t.button = b;
            mBtnDown[b] = true;
            if (b == BTN_TORCH) {
                mPlayer.torchOn = !mPlayer.torchOn && mPlayer.battery > 0.0f;
                audio.postUI(SND_FLASHLIGHT, 0.6f, mPlayer.torchOn ? 1.0f : 0.85f);
            }
            return;
        }
    }

    if (x < mR.width() * 0.46f && mStickTouch < 0) {
        t.role = 1;
        mStickTouch = slot;
    } else if (mLookTouch < 0) {
        t.role = 2;
        mLookTouch = slot;
    }
}

void Game::onTouchMove(int id, float x, float y) {
    Touch* t = findTouch(id);
    if (!t) return;
    float dx = x - t->x, dy = y - t->y;
    t->x = x; t->y = y;
    if (std::fabs(x - t->startX) + std::fabs(y - t->startY) > 12.0f * mUiScale) t->moved = true;
    if (t->role == 2) {
        float sens = 0.0038f / mUiScale;
        mPlayer.lookInput.x += dx * sens;
        mPlayer.lookInput.y += -dy * sens;
    }
}

void Game::onTouchUp(int id, float x, float y) {
    Touch* t = findTouch(id);
    if (!t) return;
    int slot = (int)(t - mTouch);
    if (t->role == 3 && t->button >= 0) mBtnDown[t->button] = false;
    if (slot == mStickTouch) mStickTouch = -1;
    if (slot == mLookTouch) mLookTouch = -1;

    if (mState == GS_TITLE && !t->moved) {
        startNewGame();
    } else if (mState == GS_DEAD && mStateTime > 2.6f) {
        // Death restarts the current job, not the game: losing an hour of
        // open-world progress is not a horror beat, it is just a punishment.
        mPlayer.reset(mWorld.playerStart);
        mFlares = std::max(mFlares, 2);
        startMission(mMission.index);
    } else if (mState == GS_MISSION_DONE && mStateTime > 2.0f) {
        if (mMission.index + 1 < MISSION_COUNT) startMission(mMission.index + 1);
        else { mState = GS_GAME_DONE; mStateTime = 0.0f; }
    } else if (mState == GS_GAME_DONE && mStateTime > 4.0f) {
        mState = GS_TITLE;
        mStateTime = 0.0f;
        mFade = 1.0f;
    } else if (mState == GS_BRIEF && mStateTime > 1.2f) {
        mState = GS_PLAY;
        mStateTime = 0.0f;
    }

    t->active = false;
    t->id = -1;
    t->role = 0;
    t->button = -1;
    (void)x; (void)y;
}

// -------------------------------------------------------------------- update

float Game::nearestMonsterDist(vec3& outPos) const {
    float best = 1e9f;
    for (int i = 0; i < MAX_MONSTERS; i++) {
        if (!mMonsters[i].active) continue;
        float d = mMonsters[i].distanceTo(mPlayer.pos);
        if (d < best) { best = d; outPos = mMonsters[i].pos; }
    }
    if (best > 1e8f) outPos = mPlayer.pos + vec3(1000.0f, 0, 0);
    return best;
}

void Game::update(float dt) {
    if (!mReady) return;
    if (dt > 0.1f) dt = 0.1f;   // a long stall must not teleport anything
    mTime += dt;
    mStateTime += dt;

    // Adaptive resolution: if the device cannot hold 40fps, shrink the 3D
    // buffer rather than the framerate.
    mFpsAccum += dt;
    mFpsFrames++;
    mGovernorTimer -= dt;
    if (mFpsAccum > 1.0f) {
        mFps = mFpsFrames / mFpsAccum;
        mFpsAccum = 0.0f;
        mFpsFrames = 0;
        if (mGovernorTimer <= 0.0f) {
            if (mFps < 38.0f && mR.renderScale() > 0.5f) {
                mR.setRenderScale(mR.renderScale() - 0.12f);
                mGovernorTimer = 5.0f;
            } else if (mFps > 56.0f && mR.renderScale() < 1.0f) {
                mR.setRenderScale(mR.renderScale() + 0.08f);
                mGovernorTimer = 6.0f;
            }
        }
    }

    if (mMessageTime > 0.0f) mMessageTime -= dt;
    if (mSubtitleTime > 0.0f) mSubtitleTime -= dt;
    if (mFlareCooldown > 0.0f) mFlareCooldown -= dt;

    switch (mState) {
    case GS_LOADING:
        break;
    case GS_TITLE:
        mFade = std::max(0.0f, mFade - dt * 1.2f);
        audio.setTension(0.12f);
        audio.setHeartRate(56.0f);
        audio.setBreath(0.0f);
        audio.setMuffle(0.0f);
        audio.setBed(mBedStreet);
        break;
    case GS_BRIEF:
        mFade = clampf(1.0f - (mStateTime - 0.5f) / 1.4f, 0.0f, 1.0f);
        audio.setTension(0.25f);
        audio.setHeartRate(64.0f);
        if (mStateTime > 5.5f) { mState = GS_PLAY; mStateTime = 0.0f; }
        break;
    case GS_PLAY:
        mFade = std::max(0.0f, mFade - dt * 1.5f);
        updatePlay(dt);
        break;
    case GS_DEAD:
        mFade = clampf((mStateTime - 1.6f) / 1.6f, 0.0f, 1.0f);
        mDamage = clampf(1.0f - mStateTime * 0.35f, 0.0f, 1.0f);
        audio.setMuffle(clampf(mStateTime * 0.8f, 0.0f, 1.0f));
        audio.setTension(clampf(1.0f - mStateTime * 0.3f, 0.0f, 1.0f));
        audio.setHeartRate(std::max(40.0f, 150.0f - mStateTime * 40.0f));
        audio.setBreath(0.0f);
        mPlayer.update(dt, mWorld, audio, 0.0f, false);
        break;
    case GS_MISSION_DONE:
        mFade = 0.0f;
        audio.setTension(std::max(0.0f, 0.35f - mStateTime * 0.1f));
        audio.setBreath(0.0f);
        audio.setMuffle(0.0f);
        break;
    case GS_GAME_DONE:
        mFade = std::max(0.0f, mFade - dt * 0.8f);
        audio.setTension(0.05f);
        audio.setBreath(0.0f);
        break;
    }
}

void Game::updatePlay(float dt) {
    if (mStickTouch >= 0 && mTouch[mStickTouch].active) {
        Touch& t = mTouch[mStickTouch];
        float maxR = 90.0f * mUiScale;
        mPlayer.moveInput = vec2(clampf((t.x - t.startX) / maxR, -1.0f, 1.0f),
                                 clampf((t.startY - t.y) / maxR, -1.0f, 1.0f));
    } else {
        mPlayer.moveInput = vec2(0, 0);
    }
    mPlayer.sprintHeld = mBtnDown[BTN_SPRINT];
    mPlayer.crouchHeld = mBtnDown[BTN_CROUCH];

    vec3 nearestPos;
    float creatureDist = nearestMonsterDist(nearestPos);

    vec3 pf = mPlayer.forward();
    pf.y = 0.0f;
    pf = normalize(pf);
    vec3 toC = nearestPos - mPlayer.pos;
    toC.y = 0.0f;
    bool inFront = creatureDist > 0.01f && creatureDist < 1e8f &&
                   dot(normalize(toC), pf) > 0.55f;
    bool creatureVisible = inFront && creatureDist < 20.0f && mPlayer.torchOn &&
                           mWorld.lineOfSight(mPlayer.eye(), nearestPos + vec3(0, 1.4f, 0));

    mPlayer.update(dt, mWorld, audio, creatureDist, creatureVisible);

    bool indoors = mWorld.indoorAt(mPlayer.pos);
    audio.setIndoor(indoors ? 1.0f : 0.0f);
    audio.setBed(indoors ? mBedInterior : mBedStreet);

    // --- flare ---
    if (mBtnDown[BTN_FLARE] && mFlares > 0 && mFlareCooldown <= 0.0f) {
        mBtnDown[BTN_FLARE] = false;
        mFlares--;
        mFlareCooldown = 1.2f;
        audio.post(SND_FLARE_FIRE, mPlayer.pos, 1.0f, 1.0f);
        mPlayer.addShake(0.4f);
        platformHaptic(HAPTIC_FLARE);
        // A flare buys distance, never a kill. There is no winning a fight in
        // this game, only choosing when it happens.
        for (int i = 0; i < MAX_MONSTERS; i++) {
            Monster& m = mMonsters[i];
            if (!m.active) continue;
            float d = m.distanceTo(mPlayer.pos);
            if (d < 16.0f && mWorld.lineOfSight(mPlayer.eye(), m.pos + vec3(0, 1.4f, 0))) {
                m.stun(2.6f + (1.0f - d / 16.0f) * 2.0f);
                m.state = AS_SEARCH;
                m.stateTimer = 0.0f;
            }
        }
        showMessage("FLARE", 1.6f);
    }

    // --- monsters ---
    for (int i = 0; i < MAX_MONSTERS; i++) {
        Monster& m = mMonsters[i];
        if (!m.active) continue;
        bool caught = m.update(dt, mWorld, mPlayer.pos, pf, mPlayer.noise,
                               mPlayer.torchOn, audio, indoors);
        // A watcher with eyes on you tells everything else where you are.
        if (m.kind == AK_WATCHER && m.state == AS_HUNT && m.alertTimer <= 0.0f) {
            m.alertTimer = 6.0f;
            for (int j = 0; j < MAX_MONSTERS; j++)
                if (j != i && mMonsters[j].active) mMonsters[j].alertTo(mPlayer.pos);
        }
        if (caught && mPlayer.alive) {
            mPlayer.alive = false;
            mState = GS_DEAD;
            mStateTime = 0.0f;
            mDamage = 1.0f;
            audio.post(SND_DEATH, mPlayer.pos, 1.0f, 1.0f);
            audio.post(SND_SCREECH, m.pos, 1.0f, 0.85f);
            platformHaptic(HAPTIC_DEATH);
            return;
        }
    }

    // --- survivors ---
    for (int i = 0; i < MAX_NPCS; i++) {
        Npc& n = mNpcs[i];
        if (!n.active) continue;
        n.update(dt, mWorld, mPlayer.pos, nearestPos, creatureDist, audio, n.rescued);
        // Monsters kill survivors too, so escorting one is a real risk rather
        // than a followed waypoint.
        for (int j = 0; j < MAX_MONSTERS; j++) {
            const Monster& m = mMonsters[j];
            if (!m.active || m.kind == AK_WATCHER) continue;
            if (n.alive && n.state != AS_DOWNED && m.distanceTo(n.pos) < 1.2f) {
                n.state = AS_DOWNED;
                n.rescued = false;
                audio.post(SND_NPC_SOB, n.pos, 1.0f, 1.0f);
                showMessage("YOU LOST THEM", 3.0f);
            }
        }
    }

    updateMission(dt);
    updateDirector(dt);

    audio.setListener(mPlayer.eye(), mPlayer.forward());
    audio.setTension(mPlayer.fear);
    audio.setHeartRate(mPlayer.heartRate());
    float breath = clampf((1.0f - mPlayer.stamina) * 0.85f + mPlayer.fear * 0.4f, 0.0f, 1.0f);
    audio.setBreath(breath);
    audio.setMuffle(clampf((mPlayer.fear - 0.82f) / 0.18f, 0.0f, 1.0f) * 0.45f);
}

void Game::updateMission(float dt) {
    if (!mMission.active) return;
    const MissionDef& d = mMission.def();
    mMission.timer += dt;
    mInteractAvailable = false;
    mInteractTarget = -1;

    switch (d.type) {
    case MT_GOTO:
    case MT_ESCAPE: {
        if (mMission.objectives.empty()) break;
        float dist = length(vec3(mMission.objectives[0].pos.x - mPlayer.pos.x, 0,
                                 mMission.objectives[0].pos.z - mPlayer.pos.z));
        if (dist < 2.4f) completeMission();
        break;
    }
    case MT_COLLECT: {
        int nearest = -1;
        float nd = 1e9f;
        for (size_t i = 0; i < mMission.objectives.size(); i++) {
            Objective& o = mMission.objectives[i];
            if (o.done) continue;
            o.bob += dt;
            float dd = length(vec3(o.pos.x - mPlayer.pos.x, 0, o.pos.z - mPlayer.pos.z));
            if (dd < nd) { nd = dd; nearest = (int)i; }
        }
        if (nearest >= 0 && nd < 1.8f) {
            mInteractAvailable = true;
            mInteractTarget = nearest;
            if (mBtnDown[BTN_INTERACT]) {
                mBtnDown[BTN_INTERACT] = false;
                mMission.objectives[nearest].done = true;
                mMission.collected++;
                audio.post(SND_PICKUP, mMission.objectives[nearest].pos, 0.85f, 1.0f);
                platformHaptic(HAPTIC_PICKUP);
                mPlayer.battery = clampf(mPlayer.battery + 0.20f, 0.0f, 1.0f);
                if (gRng.f01() < 0.4f && mFlares < 5) mFlares++;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "CACHE %d/%d",
                              mMission.collected, (int)mMission.objectives.size());
                showMessage(buf, 2.2f);
                // Taking one tells the district roughly where you are.
                for (int j = 0; j < MAX_MONSTERS; j++)
                    if (mMonsters[j].active && gRng.f01() < 0.5f)
                        mMonsters[j].alertTo(mPlayer.pos);
                if (mMission.collected >= (int)mMission.objectives.size()) completeMission();
            }
        }
        break;
    }
    case MT_ACTIVATE: {
        int nearest = -1;
        float nd = 1e9f;
        for (size_t i = 0; i < mMission.objectives.size(); i++) {
            const Objective& o = mMission.objectives[i];
            if (o.done) continue;
            float dd = length(vec3(o.pos.x - mPlayer.pos.x, 0, o.pos.z - mPlayer.pos.z));
            if (dd < nd) { nd = dd; nearest = (int)i; }
        }
        if (nearest >= 0 && nd < 1.8f) {
            mInteractAvailable = true;
            mInteractTarget = nearest;
            if (mBtnDown[BTN_INTERACT]) {
                // Held, not tapped. Standing still at a breaker for two seconds
                // while something is looking for you is the whole mechanic.
                mMission.holdTimer += dt;
                if (mMission.holdTimer >= 2.0f) {
                    mMission.holdTimer = 0.0f;
                    mBtnDown[BTN_INTERACT] = false;
                    mMission.objectives[nearest].done = true;
                    mMission.collected++;
                    audio.post(SND_EXIT_OPEN, mMission.objectives[nearest].pos, 0.9f, 1.0f);
                    for (int j = 0; j < MAX_MONSTERS; j++)
                        if (mMonsters[j].active) mMonsters[j].alertTo(mPlayer.pos);
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "BREAKER %d/%d",
                                  mMission.collected, (int)mMission.objectives.size());
                    showMessage(buf, 2.2f);
                    if (mMission.collected >= (int)mMission.objectives.size()) completeMission();
                }
            } else {
                mMission.holdTimer = std::max(0.0f, mMission.holdTimer - dt * 2.0f);
            }
        } else {
            mMission.holdTimer = 0.0f;
        }
        break;
    }
    case MT_RESCUE: {
        // Phase one: reach each survivor. Phase two: walk them to the drop-off.
        int pickedUp = 0, total = 0;
        for (const Objective& o : mMission.objectives) {
            if (o.npcId < 0 || o.npcId >= MAX_NPCS) continue;
            total++;
            Npc& n = mNpcs[o.npcId];
            if (!n.active || n.state == AS_DOWNED) continue;
            if (n.rescued) { pickedUp++; continue; }
            if (n.distanceTo(mPlayer.pos) < 2.4f) {
                mInteractAvailable = true;
                mInteractTarget = o.npcId;
                if (mBtnDown[BTN_INTERACT]) {
                    mBtnDown[BTN_INTERACT] = false;
                    n.rescued = true;
                    say("vo_sv_follow", "OKAY. I AM WITH YOU.", 3.0f);
                }
            }
        }
        if (total > 0 && pickedUp >= total) {
            float dd = length(vec3(mMission.dropOff.x - mPlayer.pos.x, 0,
                                   mMission.dropOff.z - mPlayer.pos.z));
            if (dd < 3.0f) {
                say("vo_sv_thanks", "THANK YOU. THANK YOU.", 3.0f);
                completeMission();
            }
        }
        break;
    }
    case MT_SURVIVE: {
        if (mMission.objectives.empty()) break;
        float dd = length(vec3(mMission.objectives[0].pos.x - mPlayer.pos.x, 0,
                               mMission.objectives[0].pos.z - mPlayer.pos.z));
        if (dd < 16.0f) {
            mMission.holdTimer += dt;
            if (mMission.holdTimer >= (float)d.count) completeMission();
        } else {
            // Leaving the zone stalls the clock rather than failing the job.
            mMission.holdTimer = std::max(0.0f, mMission.holdTimer - dt * 0.5f);
        }
        break;
    }
    default:
        break;
    }
}

void Game::updateDirector(float dt) {
    if (mNearMissCooldown > 0.0f) mNearMissCooldown -= dt;
    vec3 nearestPos;
    float creatureDist = nearestMonsterDist(nearestPos);

    // Environmental sounds placed at real positions, so they pan and reverberate.
    mAmbientTimer -= dt;
    if (mAmbientTimer <= 0.0f) {
        mAmbientTimer = gRng.range(5.0f, 13.0f);
        for (int tries = 0; tries < 12; tries++) {
            int x = gRng.rangei(1, mWorld.W - 1);
            int z = gRng.rangei(1, mWorld.H - 1);
            if (mWorld.solid(x, z)) continue;
            vec3 c = mWorld.cellCenter(x, z);
            float d = length(c - mPlayer.pos);
            if (d < 8.0f || d > 34.0f) continue;
            float r = gRng.f01();
            if (r < 0.4f) audio.post(SND_DRIP, c, 0.5f, gRng.range(0.85f, 1.2f));
            else if (r < 0.75f) audio.post(SND_METAL, c, 0.40f, gRng.range(0.7f, 1.3f));
            else audio.post(SND_DOOR, c, 0.35f, gRng.range(0.8f, 1.1f));
            break;
        }
    }

    mWhisperTimer -= dt;
    if (mWhisperTimer <= 0.0f) {
        mWhisperTimer = gRng.range(18.0f, 38.0f);
        if (creatureDist < 26.0f && mPlayer.fear > 0.25f) {
            // Placed just behind the player. You will turn around. There is
            // nothing there: it is still where the AI says it is.
            vec3 f = mPlayer.forward();
            f.y = 0.0f;
            vec3 behind = mPlayer.pos - normalize(f) * 2.4f + vec3(0, 1.5f, 0);
            audio.post(SND_WHISPER, behind, 0.55f, gRng.range(0.9f, 1.1f));
        }
    }

    mLightPopTimer -= dt;
    if (mLightPopTimer <= 0.0f) {
        mLightPopTimer = gRng.range(20.0f, 45.0f);
        if (creatureDist < 22.0f) {
            int best = -1;
            float bestD = 1e9f;
            for (size_t i = 0; i < mWorld.lights.size(); i++) {
                if (!mWorld.lights[i].alive) continue;
                float d = length(mWorld.lights[i].pos - mPlayer.pos);
                if (d < 18.0f && d < bestD) { bestD = d; best = (int)i; }
            }
            if (best >= 0) {
                mWorld.lights[best].alive = false;
                audio.post(SND_LIGHT_POP, mWorld.lights[best].pos, 0.85f, 1.0f);
                mPlayer.addShake(0.35f);
                platformHaptic(HAPTIC_LIGHT_POP);
            }
        }
    }

    // Radio chatter, so dispatch is a person rather than a mission-start jingle.
    mRadioTimer -= dt;
    if (mRadioTimer <= 0.0f && !audio.voiceBusy()) {
        mRadioTimer = gRng.range(45.0f, 100.0f);
        if (creatureDist < 18.0f) say("vo_warn_close", "IT IS CLOSE TO YOU. DO NOT RUN.", 4.0f);
        else if (mPlayer.battery < 0.3f) say("vo_warn_dark", "THE LIGHTS ARE GONE ON YOUR BLOCK", 4.0f);
        else if (gRng.f01() < 0.4f) say("vo_good", "GOOD. KEEP MOVING.", 3.0f);
        else say("vo_lost_signal", "I AM LOSING YOUR SIGNAL", 3.5f);
    }

    if (mNearMissCooldown <= 0.0f && creatureDist < 6.0f) {
        vec3 toC = normalize(nearestPos - mPlayer.pos);
        vec3 f = mPlayer.forward();
        f.y = 0.0f;
        if (dot(toC, normalize(f)) < -0.3f) {
            audio.post(SND_STINGER, nearestPos, 0.5f, 1.25f);
            mPlayer.addShake(0.25f);
            platformHaptic(HAPTIC_NEAR_MISS);
            mNearMissCooldown = 25.0f;
        }
    }
}

// -------------------------------------------------------------------- render

void Game::gatherLights(SceneParams& sp) {
    struct Cand { float d; int i; };
    Cand best[MAX_POINT_LIGHTS];
    int n = 0;
    for (size_t i = 0; i < mWorld.lights.size(); i++) {
        const LightSrc& L = mWorld.lights[i];
        if (!L.alive) continue;
        float d = length(L.pos - sp.camPos);
        if (d > L.radius + 28.0f) continue;
        if (n < MAX_POINT_LIGHTS) {
            best[n].d = d; best[n].i = (int)i; n++;
        } else {
            int worst = 0;
            for (int k = 1; k < n; k++) if (best[k].d > best[worst].d) worst = k;
            if (d < best[worst].d) { best[worst].d = d; best[worst].i = (int)i; }
        }
    }
    sp.numLights = n;
    for (int k = 0; k < n; k++) {
        const LightSrc& L = mWorld.lights[best[k].i];
        float f = 1.0f;
        if (L.flicker > 0.0f) {
            float a = std::sin(mTime * 17.3f + L.phase) * 0.5f + 0.5f;
            float b = std::sin(mTime * 3.1f + L.phase * 2.0f) * 0.5f + 0.5f;
            float n2 = hash2((int)(mTime * 24.0f), best[k].i);
            f = 1.0f - L.flicker * (0.55f * a * b + 0.45f * (n2 > 0.86f ? 1.0f : 0.0f));
        }
        sp.lightPos[k] = L.pos;
        sp.lightColor[k] = L.color * (0.60f * clampf(f, 0.0f, 1.0f));
        sp.lightRadius[k] = L.radius;
    }
}

void Game::renderWorld(const SceneParams& sp, bool shadowPass) {
    // Chunks are culled by frustum and distance. The shadow pass uses a much
    // tighter radius because the torch only reaches 22m anyway.
    float cullDist = shadowPass ? 26.0f : 160.0f;
    for (size_t i = 0; i < mChunks.size() && i < mChunkMeshes.size(); i++) {
        const Chunk& c = mChunks[i];
        vec3 centre = (c.mn + c.mx) * 0.5f;
        float radius = length(c.mx - c.mn) * 0.5f;
        if (length(vec3(centre.x - sp.camPos.x, 0, centre.z - sp.camPos.z)) - radius > cullDist)
            continue;
        if (!shadowPass && !mR.visible(c.mn, c.mx)) continue;
        if (shadowPass) mR.drawShadow(mChunkMeshes[i], mat4());
        else mR.draw(mChunkMeshes[i], mat4(), vec3(1, 1, 1), 0.0f);
    }
    if (shadowPass) mR.drawShadow(mPropMesh, mat4());
    else mR.draw(mPropMesh, mat4(), vec3(1, 1, 1), 0.0f);

    for (int i = 0; i < MAX_MONSTERS; i++) {
        const Monster& m = mMonsters[i];
        if (!m.active || m.state == AS_DORMANT) continue;
        if (m.distanceTo(sp.camPos) > (shadowPass ? 24.0f : 60.0f)) continue;
        for (int p = 0; p < BP_COUNT; p++) {
            if (shadowPass) mR.drawShadow(mActorParts[m.kind][p], m.parts[p]);
            else mR.draw(mActorParts[m.kind][p], m.parts[p], vec3(0.82f, 0.78f, 0.76f), 0.012f);
        }
    }
    for (int i = 0; i < MAX_NPCS; i++) {
        const Npc& n = mNpcs[i];
        if (!n.active) continue;
        if (length(n.pos - sp.camPos) > (shadowPass ? 24.0f : 60.0f)) continue;
        for (int p = 0; p < BP_COUNT; p++) {
            if (shadowPass) mR.drawShadow(mActorParts[AK_SURVIVOR][p], n.parts[p]);
            else mR.draw(mActorParts[AK_SURVIVOR][p], n.parts[p], vec3(0.90f, 0.88f, 0.86f), 0.0f);
        }
    }

    if (shadowPass) return;

    const MissionDef& d = mMission.def();
    for (const Objective& o : mMission.objectives) {
        if (o.done || o.npcId >= 0) continue;
        if (d.type == MT_ACTIVATE) {
            mR.draw(mDeviceMesh, mat4::translation(o.pos), vec3(0.9f, 0.85f, 0.6f), 0.30f);
        } else if (d.type == MT_COLLECT) {
            float bob = std::sin(o.bob * 1.7f) * 0.06f;
            mR.draw(mPickupMesh, mat4::translation(o.pos + vec3(0, bob, 0))
                                 * mat4::rotationY(o.bob * 0.9f),
                    vec3(0.88f, 0.90f, 0.72f), 0.42f);
        }
    }
    if (mMission.active) {
        vec3 goal = mMission.marker;
        if (d.type == MT_RESCUE) {
            bool allPicked = true;
            for (const Objective& o : mMission.objectives)
                if (o.npcId >= 0 && o.npcId < MAX_NPCS && !mNpcs[o.npcId].rescued) {
                    allPicked = false;
                    goal = mNpcs[o.npcId].pos;
                    break;
                }
            if (allPicked) goal = mMission.dropOff;
        } else {
            for (const Objective& o : mMission.objectives)
                if (!o.done) { goal = o.pos; break; }
        }
        float pulse = 0.35f + 0.20f * std::sin(mTime * 2.2f);
        mR.draw(mMarkerMesh, mat4::translation(vec3(goal.x, 0.0f, goal.z)),
                vec3(0.45f, 0.95f, 0.60f), pulse);
    }
}

void Game::render() {
    if (!mReady) return;

    SceneParams sp;
    sp.time = mTime;
    bool indoors = mWorld.indoorAt(mPlayer.pos);
    // Outdoors the fog has to be thin or the city stops existing; indoors it
    // closes right in, which is what makes going inside feel different.
    sp.fogColor = indoors ? vec3(0.014f, 0.014f, 0.016f) : vec3(0.020f, 0.022f, 0.030f);
    sp.fogDensity = indoors ? 0.085f : 0.030f;

    bool inLevel = (mState == GS_PLAY || mState == GS_BRIEF || mState == GS_DEAD ||
                    mState == GS_MISSION_DONE);

    if (inLevel) {
        vec3 eye = mPlayer.eye();
        vec3 fwd = mPlayer.forward();
        vec3 up(0, 1, 0);
        if (mState == GS_DEAD) {
            vec3 nearestPos;
            nearestMonsterDist(nearestPos);
            vec3 toC = nearestPos + vec3(0, 1.5f, 0) - eye;
            if (lengthSq(toC) > 0.01f)
                fwd = normalize(lerp(fwd, normalize(toC), clampf(mStateTime * 1.6f, 0.0f, 1.0f)));
            float roll = clampf(mStateTime * 0.55f, 0.0f, 0.85f);
            up = vec3(std::sin(roll), std::cos(roll), 0);
        } else if (mPlayer.shake > 0.001f) {
            float k = mPlayer.shake * 0.03f;
            fwd = normalize(fwd + vec3(std::sin(mTime * 63.0f) * k, std::cos(mTime * 71.0f) * k, 0));
        }

        sp.camPos = eye;
        sp.view = mat4::lookAt(eye, eye + fwd, up);
        float aspect = (float)mR.width() / (float)std::max(1, mR.height());
        sp.proj = mat4::perspective(72.0f * DEG2RAD, aspect, 0.06f, 220.0f);

        sp.torchPos = eye;
        sp.torchDir = mPlayer.torchDir(mTime);
        float flick = mPlayer.torchFlicker;
        vec3 nearestPos;
        float cd = nearestMonsterDist(nearestPos);
        if (cd < 9.0f) {
            // Proximity interference: the beam stutters before you have any
            // other reason to know something is near.
            float k = 1.0f - cd / 9.0f;
            flick *= 1.0f - k * 0.45f * (0.5f + 0.5f * std::sin(mTime * 31.0f));
        }
        sp.torchIntensity = mPlayer.torchOn ? 2.15f * clampf(flick, 0.0f, 1.0f) : 0.0f;
        sp.torchRange = 22.0f;
        sp.torchColor = vec3(1.0f, 0.93f, 0.80f);
        // A little moonlight outdoors; effectively none indoors.
        sp.ambient = indoors ? vec3(0.012f, 0.013f, 0.017f) : vec3(0.030f, 0.034f, 0.048f);
        gatherLights(sp);

        mR.beginShadowPass(sp);
        if (sp.torchIntensity > 0.01f) renderWorld(sp, true);
        mR.endShadowPass();

        mR.beginScene(sp);
        renderWorld(sp, false);
        mR.endScene();
        mR.postProcess(mPlayer.fear, mTime, mFade, mDamage);
    } else {
        sp.camPos = vec3(0, 1.6f, 0);
        sp.view = mat4::lookAt(sp.camPos, sp.camPos + vec3(0, 0, 1), vec3(0, 1, 0));
        sp.proj = mat4::perspective(70.0f * DEG2RAD,
                                    (float)mR.width() / (float)std::max(1, mR.height()), 0.1f, 50.0f);
        sp.torchIntensity = 0.0f;
        sp.numLights = 0;
        sp.fogColor = vec3(0.012f, 0.012f, 0.016f);
        mR.beginShadowPass(sp);
        mR.endShadowPass();
        mR.beginScene(sp);
        mR.endScene();
        mR.postProcess(0.18f, mTime, mFade, 0.0f);
    }

    mR.uiBegin();
    if (mState == GS_TITLE) renderTitle();
    else if (mState == GS_PLAY) renderHud();
    renderOverlayText();
    mR.uiEnd();
}

void Game::renderTitle() {
    float s = mUiScale;
    float w = (float)mR.width(), h = (float)mR.height();
    float pulse = 0.75f + 0.25f * std::sin(mTime * 1.1f);

    mR.uiTextCentered("HOLLOW SIGNAL", w * 0.5f, h * 0.22f, 58.0f * s, 0.86f, 0.84f, 0.80f, pulse);
    mR.uiQuad(w * 0.5f - 170.0f * s, h * 0.22f + 76.0f * s, 340.0f * s, 1.5f * s,
              0.35f, 0.36f, 0.38f, 0.7f);
    mR.uiTextCentered("AN OPEN CITY. SIXTEEN JOBS. ONE WAY OUT.",
                      w * 0.5f, h * 0.22f + 98.0f * s, 16.0f * s, 0.45f, 0.46f, 0.48f, 0.9f);

    char line[96];
    std::snprintf(line, sizeof(line), "PROGRESS  %d / %d", mUnlocked, MISSION_COUNT);
    mR.uiTextCentered(line, w * 0.5f, h * 0.55f, 20.0f * s, 0.70f, 0.70f, 0.68f, 0.9f);

    mR.uiTextCentered("TAP TO BEGIN", w * 0.5f, h - 110.0f * s, 20.0f * s,
                      0.60f, 0.60f, 0.60f, 0.5f + 0.35f * std::sin(mTime * 2.2f));
    mR.uiTextCentered("HEADPHONES RECOMMENDED", w * 0.5f, h - 64.0f * s, 13.0f * s,
                      0.38f, 0.38f, 0.40f, 0.7f);
}

void Game::renderCompass() {
    float s = mUiScale;
    float w = (float)mR.width();
    float cx = w * 0.5f, y = 26.0f * s;
    float halfW = 180.0f * s;
    mR.uiQuad(cx - halfW, y, halfW * 2.0f, 2.0f * s, 0.6f, 0.6f, 0.62f, 0.22f);
    if (!mMission.active) return;

    vec3 goal = mMission.marker;
    const MissionDef& d = mMission.def();
    if (d.type == MT_RESCUE) {
        bool allPicked = true;
        for (const Objective& o : mMission.objectives)
            if (o.npcId >= 0 && o.npcId < MAX_NPCS && !mNpcs[o.npcId].rescued) {
                allPicked = false;
                goal = mNpcs[o.npcId].pos;
                break;
            }
        if (allPicked) goal = mMission.dropOff;
    } else {
        for (const Objective& o : mMission.objectives)
            if (!o.done) { goal = o.pos; break; }
    }

    vec3 to = goal - mPlayer.pos;
    to.y = 0.0f;
    float rel = std::atan2(to.x, to.z) - mPlayer.yaw;
    while (rel > PI) rel -= TAU;
    while (rel < -PI) rel += TAU;

    // Clamped to the ends of the strip so the marker never vanishes entirely.
    float t = clampf(rel / (PI * 0.6f), -1.0f, 1.0f);
    float mx = cx + t * halfW;
    float a = (std::fabs(rel) > PI * 0.6f) ? 0.45f : 0.95f;
    mR.uiDisc(mx, y + 1.0f * s, 7.0f * s, 0.45f, 0.95f, 0.60f, a);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%dM", (int)length(to));
    mR.uiTextCentered(buf, mx, y + 14.0f * s, 13.0f * s, 0.55f, 0.92f, 0.62f, a);
}

void Game::renderMiniMap() {
    float s = mUiScale;
    float w = (float)mR.width();
    float size = 132.0f * s;
    float mx = w - size - 22.0f * s;
    float my = 22.0f * s;
    const float range = 46.0f;   // metres shown across the map

    mR.uiQuad(mx, my, size, size, 0.03f, 0.035f, 0.04f, 0.55f);

    int pcx, pcz;
    mWorld.worldToCell(mPlayer.pos, pcx, pcz);
    int cells = (int)(range / mWorld.cellSize);
    float px = size / (cells * 2.0f);
    float cs = std::cos(-mPlayer.yaw), sn = std::sin(-mPlayer.yaw);
    for (int dz = -cells; dz < cells; dz++) {
        for (int dx = -cells; dx < cells; dx++) {
            int x = pcx + dx, z = pcz + dz;
            if (!mWorld.inBounds(x, z) || !mWorld.solid(x, z)) continue;
            // Rotate into view space so the map turns with the player.
            float rx = dx * cs - dz * sn;
            float rz = dx * sn + dz * cs;
            float sx = mx + size * 0.5f + rx * px;
            float sy = my + size * 0.5f + rz * px;
            if (sx < mx || sx > mx + size - px || sy < my || sy > my + size - px) continue;
            mR.uiQuad(sx, sy, px + 0.6f, px + 0.6f, 0.30f, 0.31f, 0.34f, 0.75f);
        }
    }

    mR.uiDisc(mx + size * 0.5f, my + size * 0.5f, 4.0f * s, 0.9f, 0.9f, 0.92f, 0.95f);

    float scale = px / mWorld.cellSize;
    if (mMission.active) {
        vec3 to = mMission.marker - mPlayer.pos;
        float rx = (to.x * cs - to.z * sn) * scale;
        float ry = (to.x * sn + to.z * cs) * scale;
        float lim = size * 0.46f;
        float l = std::sqrt(rx * rx + ry * ry);
        if (l > lim && l > 0.001f) { rx = rx / l * lim; ry = ry / l * lim; }
        mR.uiDisc(mx + size * 0.5f + rx, my + size * 0.5f + ry, 5.0f * s,
                  0.45f, 0.95f, 0.60f, 0.95f);
    }

    // Only monsters close enough that you would already hear them. Showing all
    // of them would turn the game into a tracking exercise and kill the dread.
    for (int i = 0; i < MAX_MONSTERS; i++) {
        const Monster& m = mMonsters[i];
        if (!m.active) continue;
        float d = m.distanceTo(mPlayer.pos);
        if (d > 18.0f) continue;
        vec3 to = m.pos - mPlayer.pos;
        float rx = (to.x * cs - to.z * sn) * scale;
        float ry = (to.x * sn + to.z * cs) * scale;
        mR.uiDisc(mx + size * 0.5f + rx, my + size * 0.5f + ry, 4.0f * s,
                  0.85f, 0.20f, 0.18f, 0.35f + 0.45f * (1.0f - d / 18.0f));
    }
}

void Game::renderHud() {
    float s = mUiScale;
    float h = (float)mR.height();
    const MissionDef& d = mMission.def();

    renderCompass();
    renderMiniMap();

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%02d  %s", mMission.index + 1, d.title);
    mR.uiText(buf, 26.0f * s, 26.0f * s, 17.0f * s, 0.62f, 0.63f, 0.60f, 0.85f);

    if (d.type == MT_COLLECT || d.type == MT_ACTIVATE)
        std::snprintf(buf, sizeof(buf), "%s  %d/%d", d.objective,
                      mMission.collected, (int)mMission.objectives.size());
    else if (d.type == MT_SURVIVE)
        std::snprintf(buf, sizeof(buf), "%s  %ds", d.objective,
                      std::max(0, d.count - (int)mMission.holdTimer));
    else
        std::snprintf(buf, sizeof(buf), "%s", d.objective);
    mR.uiText(buf, 26.0f * s, 48.0f * s, 21.0f * s, 0.86f, 0.85f, 0.80f, 0.92f);

    float bx = 26.0f * s, by = h - 58.0f * s, bw = 190.0f * s, bh = 9.0f * s;
    mR.uiQuad(bx, by, bw, bh, 0.10f, 0.10f, 0.11f, 0.65f);
    bool low = mPlayer.battery < 0.25f;
    mR.uiQuad(bx, by, bw * mPlayer.battery, bh,
              low ? 1.0f : 0.85f, low ? 0.34f : 0.82f, 0.45f, 0.9f);
    mR.uiText("TORCH", bx, by - 21.0f * s, 12.0f * s, 0.55f, 0.55f, 0.55f, 0.7f);

    if (mPlayer.stamina < 0.999f) {
        float sy = by - 40.0f * s;
        mR.uiQuad(bx, sy, bw, bh * 0.7f, 0.10f, 0.10f, 0.11f, 0.55f);
        mR.uiQuad(bx, sy, bw * mPlayer.stamina, bh * 0.7f, 0.55f, 0.72f, 0.85f, 0.8f);
    }

    std::snprintf(buf, sizeof(buf), "FLARES  %d", mFlares);
    mR.uiText(buf, bx, by - 62.0f * s, 14.0f * s, 0.80f, 0.62f, 0.35f, 0.85f);

    if (mStickTouch >= 0 && mTouch[mStickTouch].active) {
        Touch& t = mTouch[mStickTouch];
        float maxR = 90.0f * s;
        mR.uiRing(t.startX, t.startY, maxR, 2.5f * s, 0.75f, 0.75f, 0.78f, 0.22f);
        float dx = t.x - t.startX, dy = t.y - t.startY;
        float dl = std::sqrt(dx * dx + dy * dy);
        if (dl > maxR) { dx = dx / dl * maxR; dy = dy / dl * maxR; }
        mR.uiDisc(t.startX + dx, t.startY + dy, 30.0f * s, 0.85f, 0.85f, 0.88f, 0.30f);
    }

    struct { int id; const char* label; } btns[4] = {
        {BTN_SPRINT, "RUN"}, {BTN_CROUCH, "CROUCH"}, {BTN_TORCH, "LIGHT"}, {BTN_FLARE, "FLARE"}
    };
    for (int i = 0; i < 4; i++) {
        int b = btns[i].id;
        float a = mBtnDown[b] ? 0.42f : 0.20f;
        bool disabled = (b == BTN_SPRINT && mPlayer.stamina < 0.06f) ||
                        (b == BTN_TORCH && mPlayer.battery <= 0.0f) ||
                        (b == BTN_FLARE && mFlares <= 0);
        if (disabled) a *= 0.4f;
        mR.uiDisc(mBtnX[b], mBtnY[b], mBtnR[b], 0.80f, 0.80f, 0.84f, a);
        mR.uiRing(mBtnX[b], mBtnY[b], mBtnR[b], 2.0f * s, 0.85f, 0.85f, 0.88f, a + 0.15f);
        float tw = mR.textWidth(btns[i].label, 13.0f * s);
        mR.uiText(btns[i].label, mBtnX[b] - tw * 0.5f, mBtnY[b] - 6.5f * s, 13.0f * s,
                  0.92f, 0.92f, 0.94f, disabled ? 0.35f : 0.85f);
    }

    if (mInteractAvailable) {
        float pulse = 0.55f + 0.25f * std::sin(mTime * 5.0f);
        int b = BTN_INTERACT;
        mR.uiDisc(mBtnX[b], mBtnY[b], mBtnR[b], 0.85f, 0.86f, 0.62f, 0.30f * pulse);
        mR.uiRing(mBtnX[b], mBtnY[b], mBtnR[b], 2.5f * s, 0.90f, 0.90f, 0.66f, pulse);
        const char* label = (d.type == MT_ACTIVATE) ? "HOLD"
                          : (d.type == MT_RESCUE) ? "HELP" : "TAKE";
        float tw = mR.textWidth(label, 16.0f * s);
        mR.uiText(label, mBtnX[b] - tw * 0.5f, mBtnY[b] - 8.0f * s, 16.0f * s,
                  0.95f, 0.95f, 0.80f, 0.95f);
        if (d.type == MT_ACTIVATE && mMission.holdTimer > 0.0f)
            mR.uiRing(mBtnX[b], mBtnY[b], mBtnR[b] + 8.0f * s, 4.0f * s,
                      0.55f, 0.95f, 0.62f, clampf(mMission.holdTimer / 2.0f, 0.0f, 1.0f));
    }
}

void Game::renderOverlayText() {
    float s = mUiScale;
    float w = (float)mR.width(), h = (float)mR.height();
    const MissionDef& d = mMission.def();

    if (mState == GS_BRIEF) {
        float a = clampf(mStateTime / 0.9f, 0.0f, 1.0f) *
                  clampf((5.5f - mStateTime) / 0.8f, 0.0f, 1.0f);
        char num[48];
        std::snprintf(num, sizeof(num), "MISSION %02d", mMission.index + 1);
        mR.uiTextCentered(num, w * 0.5f, h * 0.38f, 16.0f * s, 0.45f, 0.46f, 0.48f, a);
        mR.uiTextCentered(d.title, w * 0.5f, h * 0.38f + 32.0f * s, 42.0f * s,
                          0.85f, 0.83f, 0.79f, a);
        mR.uiTextCentered(d.objective, w * 0.5f, h * 0.38f + 92.0f * s, 17.0f * s,
                          0.52f, 0.53f, 0.54f, a);
    } else if (mState == GS_DEAD) {
        float a = clampf((mStateTime - 1.8f) / 1.0f, 0.0f, 1.0f);
        mR.uiTextCentered("IT FOUND YOU", w * 0.5f, h * 0.44f, 40.0f * s, 0.72f, 0.16f, 0.14f, a);
        if (mStateTime > 2.6f)
            mR.uiTextCentered("TAP TO RETRY THE MISSION", w * 0.5f, h * 0.44f + 62.0f * s,
                              16.0f * s, 0.55f, 0.55f, 0.55f,
                              a * (0.55f + 0.35f * std::sin(mTime * 3.0f)));
    } else if (mState == GS_MISSION_DONE) {
        float a = clampf(mStateTime / 1.0f, 0.0f, 1.0f);
        mR.uiTextCentered("OBJECTIVE COMPLETE", w * 0.5f, h * 0.42f, 34.0f * s,
                          0.62f, 0.86f, 0.66f, a);
        mR.uiTextCentered(d.title, w * 0.5f, h * 0.42f + 48.0f * s, 18.0f * s,
                          0.48f, 0.50f, 0.50f, a);
        if (mStateTime > 2.0f) {
            const char* next = (mMission.index + 1 < MISSION_COUNT)
                             ? "TAP FOR THE NEXT JOB" : "TAP TO CONTINUE";
            mR.uiTextCentered(next, w * 0.5f, h * 0.42f + 92.0f * s, 16.0f * s,
                              0.55f, 0.55f, 0.55f,
                              a * (0.55f + 0.35f * std::sin(mTime * 3.0f)));
        }
    } else if (mState == GS_GAME_DONE) {
        float a = clampf(mStateTime / 1.4f, 0.0f, 1.0f);
        mR.uiTextCentered("YOU REACHED THE HARBOUR", w * 0.5f, h * 0.34f, 36.0f * s,
                          0.84f, 0.82f, 0.78f, a);
        mR.uiTextCentered("THE DISTRICT IS BEHIND YOU", w * 0.5f, h * 0.34f + 56.0f * s,
                          18.0f * s, 0.55f, 0.56f, 0.56f, a);
        mR.uiTextCentered("NOTHING FOLLOWED YOU OUT", w * 0.5f, h * 0.34f + 84.0f * s,
                          18.0f * s, 0.55f, 0.56f, 0.56f,
                          a * clampf(mStateTime - 2.0f, 0.0f, 1.0f));
        mR.uiTextCentered("THAT IS WHAT WORRIES YOU", w * 0.5f, h * 0.34f + 112.0f * s,
                          18.0f * s, 0.50f, 0.40f, 0.40f,
                          a * clampf(mStateTime - 3.6f, 0.0f, 1.0f));
    }

    if (mMessageTime > 0.0f && mState == GS_PLAY) {
        float a = clampf(mMessageTime, 0.0f, 1.0f);
        mR.uiTextCentered(mMessage.c_str(), w * 0.5f, h * 0.70f, 20.0f * s,
                          0.82f, 0.80f, 0.72f, a * 0.9f);
    }
    // Every spoken line is captioned, always.
    if (mSubtitleTime > 0.0f && !mSubtitle.empty()) {
        float a = clampf(mSubtitleTime, 0.0f, 1.0f);
        float tw = mR.textWidth(mSubtitle.c_str(), 16.0f * s);
        mR.uiQuad(w * 0.5f - tw * 0.5f - 14.0f * s, h * 0.845f - 6.0f * s,
                  tw + 28.0f * s, 30.0f * s, 0.0f, 0.0f, 0.0f, 0.45f * a);
        mR.uiTextCentered(mSubtitle.c_str(), w * 0.5f, h * 0.85f, 16.0f * s,
                          0.88f, 0.88f, 0.84f, a);
    }
}

} // namespace hm
