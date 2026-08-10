#include "game.h"
#include "noise.h"
#include <android/log.h>
#include <cstdio>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HollowGame", __VA_ARGS__)

namespace hm {

static Rng gRng(0xC0FFEEu);

// ------------------------------------------------------------------ lifecycle

bool Game::init(const char* dataPath) {
    mSavePath = std::string(dataPath ? dataPath : ".") + "/progress.dat";
    loadProgress();
    audio.start();
    audio.setMasterGain(1.0f);
    mState = GS_TITLE;
    mStateTime = 0.0f;
    mFade = 1.0f;
    return true;
}

void Game::shutdown() {
    saveProgress();
    audio.stop();
    mWorldMesh.destroy();
    mPropMesh.destroy();
    mPickupMesh.destroy();
    mExitMesh.destroy();
    for (int i = 0; i < BP_COUNT; i++) mStalkerParts[i].destroy();
    mR.shutdown();
    mReady = false;
}

void Game::loadProgress() {
    mUnlocked = 1;
    FILE* f = std::fopen(mSavePath.c_str(), "rb");
    if (!f) return;
    int v = 0, u = 1;
    if (std::fread(&v, sizeof(int), 1, f) == 1 && v == 1) {
        if (std::fread(&u, sizeof(int), 1, f) == 1) {
            mUnlocked = clampf((float)u, 1.0f, (float)CHAPTER_COUNT);
        }
    }
    std::fclose(f);
}

void Game::saveProgress() {
    FILE* f = std::fopen(mSavePath.c_str(), "wb");
    if (!f) return;
    int v = 1;
    std::fwrite(&v, sizeof(int), 1, f);
    std::fwrite(&mUnlocked, sizeof(int), 1, f);
    std::fclose(f);
}

void Game::surfaceCreated() {
    // The GL context is recreated whenever the window comes back, so every GPU
    // resource has to be rebuilt here - not just on first launch. Drop the old
    // handles before re-uploading: they belong to a context that no longer
    // exists, and upload() would otherwise see a non-zero VAO and reuse it.
    mWorldMesh.orphan();
    mPropMesh.orphan();
    mPickupMesh.orphan();
    mExitMesh.orphan();
    for (int i = 0; i < BP_COUNT; i++) mStalkerParts[i].orphan();
    mReady = false;

    if (!mR.init()) {
        LOGI("renderer init failed");
        return;
    }
    Mesh parts[BP_COUNT];
    buildStalkerParts(parts);
    for (int i = 0; i < BP_COUNT; i++) mStalkerParts[i].upload(parts[i]);
    buildPickupMesh();
    buildExitMesh();
    if (!mWorld.cells.empty()) {
        Mesh m;
        mWorld.buildMesh(m);
        mWorldMesh.upload(m);
        buildPropsMesh();
    }
    mReady = true;
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
    if (mState == GS_PLAY || mState == GS_INTRO) {
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
    Mesh crate, barrel, locker, pipe, gurney;

    crate.box({-0.30f, 0.0f, -0.30f}, {0.30f, 0.58f, 0.30f}, 1.6f);
    barrel.taperedPrism({0, 0, 0}, 0.88f, 0.28f, 0.26f, 10);
    locker.box({-0.26f, 0.0f, -0.19f}, {0.26f, 1.85f, 0.19f}, 1.2f);
    pipe.taperedPrism({0, 0, 0}, 3.2f, 0.085f, 0.085f, 6);
    // Gurney: a top plate on four thin legs.
    gurney.box({-0.34f, 0.62f, -0.86f}, {0.34f, 0.70f, 0.86f}, 1.4f);
    for (int i = 0; i < 4; i++) {
        float sx = (i & 1) ? 0.28f : -0.28f;
        float sz = (i & 2) ? 0.78f : -0.78f;
        gurney.box({sx - 0.035f, 0.0f, sz - 0.035f}, {sx + 0.035f, 0.62f, sz + 0.035f}, 3.0f);
    }

    const Mesh* kinds[5] = {&crate, &barrel, &locker, &pipe, &gurney};
    for (const Prop& p : mWorld.props) {
        mat4 xf = mat4::translation(p.pos) * mat4::rotationY(p.yaw);
        out.append(*kinds[p.kind % 5], xf);
        // Stack a second crate now and then so silhouettes vary.
        if (p.kind == 0 && ((int)(p.yaw * 100.0f) & 3) == 0) {
            out.append(crate, mat4::translation(p.pos + vec3(0.05f, 0.58f, 0.03f))
                              * mat4::rotationY(p.yaw * 1.7f));
        }
    }
    mPropMesh.upload(out);
}

void Game::buildPickupMesh() {
    // A small angular canister. Reads as man-made at a glance, which is the
    // point: it should be the only clean object in the room.
    Mesh m;
    m.taperedPrism({0, -0.10f, 0}, 0.20f, 0.055f, 0.055f, 6);
    m.ellipsoid({0, 0.13f, 0}, {0.045f, 0.035f, 0.045f}, 8, 6);
    mPickupMesh.upload(m);
}

void Game::buildExitMesh() {
    Mesh m;
    const float w = 0.62f, h = 2.35f, t = 0.14f;
    m.box({-w - t, 0.0f, -t}, {-w, h, t}, 1.5f);
    m.box({w, 0.0f, -t}, {w + t, h, t}, 1.5f);
    m.box({-w - t, h, -t}, {w + t, h + t, t}, 1.5f);
    mExitMesh.upload(m);
}

// ------------------------------------------------------------------ chapters

void Game::startChapter(int index) {
    mChapter = clampf((float)index, 0.0f, (float)(CHAPTER_COUNT - 1));
    const ChapterSpec& spec = chapterSpec(mChapter);

    mWorld.generate(spec);
    Mesh m;
    mWorld.buildMesh(m);
    mWorldMesh.upload(m);
    buildPropsMesh();

    mPlayer.reset(mWorld.playerStart);
    // Face the player down the most open direction so episode one does not open
    // with your nose against a wall.
    {
        int cx, cz;
        mWorld.worldToCell(mPlayer.pos, cx, cz);
        const int dx[4] = {0, 1, 0, -1};
        const int dz[4] = {1, 0, -1, 0};
        int bestD = 0, bestOpen = -1;
        for (int d = 0; d < 4; d++) {
            int open = 0;
            for (int step = 1; step < 8; step++) {
                if (mWorld.solid(cx + dx[d] * step, cz + dz[d] * step)) break;
                open++;
            }
            if (open > bestOpen) { bestOpen = open; bestD = d; }
        }
        mPlayer.yaw = std::atan2((float)dx[bestD], (float)dz[bestD]);
    }

    mStalker.reset(mWorld, mPlayer.pos, spec);
    mCollected = 0;
    mExitActive = false;
    mDamage = 0.0f;
    mDeathTimer = 0.0f;
    mAmbientTimer = 4.0f;
    mWhisperTimer = 22.0f;
    mLightPopTimer = 28.0f;
    mNearMissCooldown = 0.0f;
    mState = GS_INTRO;
    mStateTime = 0.0f;
    mFade = 1.0f;
    mMessage.clear();
    mMessageTime = 0.0f;
    LOGI("chapter %d '%s' - %dx%d, %d objectives, %d lights, %d props",
         mChapter + 1, spec.title, mWorld.W, mWorld.H,
         (int)mWorld.pickups.size(), (int)mWorld.lights.size(), (int)mWorld.props.size());
}

void Game::showMessage(const char* s, float seconds) {
    mMessage = s;
    mMessageTime = seconds;
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
    mBtnX[BTN_CROUCH] = w - 250.0f * s; mBtnY[BTN_CROUCH] = h - 92.0f * s;  mBtnR[BTN_CROUCH] = 50.0f * s;
    mBtnX[BTN_TORCH]  = w - 108.0f * s; mBtnY[BTN_TORCH]  = h - 262.0f * s; mBtnR[BTN_TORCH]  = 50.0f * s;
    mBtnX[BTN_INTERACT] = w * 0.5f;     mBtnY[BTN_INTERACT] = h - 130.0f * s; mBtnR[BTN_INTERACT] = 62.0f * s;
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

    // Buttons win over everything else they overlap.
    for (int b = 0; b < BTN_COUNT; b++) {
        if (b == BTN_INTERACT && !mInteractAvailable) continue;
        float dx = x - mBtnX[b], dy = y - mBtnY[b];
        if (dx * dx + dy * dy <= mBtnR[b] * mBtnR[b] * 1.35f) {
            t.role = 3; t.button = b;
            mBtnDown[b] = true;
            if (b == BTN_TORCH) {
                mPlayer.torchOn = !mPlayer.torchOn && mPlayer.battery > 0.0f;
                audio.postUI(SND_FLASHLIGHT, 0.6f, mPlayer.torchOn ? 1.0f : 0.85f);
            } else if (b == BTN_INTERACT) {
                // Handled in updatePlay so range is re-checked at pickup time.
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
        // Look. Sensitivity is resolution-independent.
        float sens = 0.0038f / mUiScale;
        mPlayer.lookInput.x += dx * sens;
        mPlayer.lookInput.y += -dy * sens;
    }
}

void Game::onTouchUp(int id, float x, float y) {
    Touch* t = findTouch(id);
    if (!t) return;
    int slot = (int)(t - mTouch);
    if (t->role == 3 && t->button >= 0) {
        mBtnDown[t->button] = false;
        if (t->button == BTN_INTERACT) {
            // consumed in updatePlay
        }
    }
    if (slot == mStickTouch) mStickTouch = -1;
    if (slot == mLookTouch) mLookTouch = -1;

    if (mState == GS_TITLE && !t->moved) {
        // Chapter list hit-testing.
        float s = mUiScale;
        float h = (float)mR.height();
        float y0 = h * 0.42f;
        for (int i = 0; i < CHAPTER_COUNT; i++) {
            float ry = y0 + i * 58.0f * s;
            if (y >= ry - 10.0f * s && y <= ry + 44.0f * s) {
                if (i < mUnlocked) {
                    startChapter(i);
                    audio.postUI(SND_DOOR, 0.5f, 1.2f);
                } else {
                    audio.postUI(SND_FLASHLIGHT, 0.4f, 0.7f);
                }
                break;
            }
        }
    } else if ((mState == GS_DEAD && mStateTime > 2.6f) ||
               (mState == GS_CHAPTER_DONE && mStateTime > 2.2f) ||
               (mState == GS_SERIES_DONE && mStateTime > 3.0f)) {
        if (mState == GS_DEAD) {
            startChapter(mChapter);
        } else if (mState == GS_CHAPTER_DONE) {
            if (mChapter + 1 < CHAPTER_COUNT) startChapter(mChapter + 1);
            else { mState = GS_SERIES_DONE; mStateTime = 0.0f; }
        } else {
            mState = GS_TITLE; mStateTime = 0.0f; mFade = 1.0f;
        }
    } else if (mState == GS_INTRO && mStateTime > 1.0f) {
        mState = GS_PLAY;
        mStateTime = 0.0f;
    }

    t->active = false;
    t->id = -1;
    t->role = 0;
    t->button = -1;
}

// -------------------------------------------------------------------- update

void Game::update(float dt) {
    if (!mReady) return;
    if (dt > 0.1f) dt = 0.1f;   // a long stall must not teleport the stalker
    mTime += dt;
    mStateTime += dt;

    // Adaptive resolution: if the device cannot hold 40fps, drop the 3D buffer
    // rather than the framerate. This is what makes "runs on anything" true.
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

    switch (mState) {
    case GS_TITLE:
        mFade = std::max(0.0f, mFade - dt * 1.2f);
        audio.setTension(0.12f);
        audio.setHeartRate(56.0f);
        audio.setBreath(0.0f);
        audio.setMuffle(0.0f);
        break;

    case GS_INTRO:
        mFade = clampf(1.0f - (mStateTime - 0.6f) / 1.4f, 0.0f, 1.0f);
        audio.setTension(0.25f);
        audio.setHeartRate(64.0f);
        if (mStateTime > 4.2f) { mState = GS_PLAY; mStateTime = 0.0f; }
        break;

    case GS_PLAY:
        mFade = std::max(0.0f, mFade - dt * 1.5f);
        updatePlay(dt);
        break;

    case GS_DEAD:
        mDeathTimer += dt;
        mFade = clampf((mDeathTimer - 1.6f) / 1.6f, 0.0f, 1.0f);
        mDamage = clampf(1.0f - mDeathTimer * 0.35f, 0.0f, 1.0f);
        audio.setMuffle(clampf(mDeathTimer * 0.8f, 0.0f, 1.0f));
        audio.setTension(clampf(1.0f - mDeathTimer * 0.3f, 0.0f, 1.0f));
        audio.setHeartRate(std::max(40.0f, 150.0f - mDeathTimer * 40.0f));
        audio.setBreath(0.0f);
        mPlayer.update(dt, mWorld, audio, 0.0f, false);
        break;

    case GS_CHAPTER_DONE:
        mFade = clampf((mStateTime - 1.2f) / 1.5f, 0.0f, 1.0f);
        audio.setTension(std::max(0.0f, 0.4f - mStateTime * 0.1f));
        audio.setBreath(0.0f);
        audio.setMuffle(0.0f);
        break;

    case GS_SERIES_DONE:
        mFade = std::max(0.0f, mFade - dt * 0.8f);
        audio.setTension(0.05f);
        audio.setBreath(0.0f);
        break;
    }
}

void Game::updatePlay(float dt) {
    // --- stick input ---
    if (mStickTouch >= 0 && mTouch[mStickTouch].active) {
        Touch& t = mTouch[mStickTouch];
        float maxR = 90.0f * mUiScale;
        float dx = (t.x - t.startX) / maxR;
        float dy = (t.startY - t.y) / maxR;
        mPlayer.moveInput = vec2(clampf(dx, -1.0f, 1.0f), clampf(dy, -1.0f, 1.0f));
    } else {
        mPlayer.moveInput = vec2(0, 0);
    }
    mPlayer.sprintHeld = mBtnDown[BTN_SPRINT];
    mPlayer.crouchHeld = mBtnDown[BTN_CROUCH];

    float creatureDist = mStalker.distanceTo(mPlayer.pos);
    // "Visible" for fear purposes means lit and in front of you.
    vec3 toC = mStalker.pos - mPlayer.pos;
    toC.y = 0.0f;
    vec3 pf = mPlayer.forward();
    pf.y = 0.0f;
    pf = normalize(pf);
    bool inFront = creatureDist > 0.01f && dot(normalize(toC), pf) > 0.55f;
    bool creatureVisible = inFront && creatureDist < 18.0f && mPlayer.torchOn &&
                           mWorld.lineOfSight(mPlayer.eye(), mStalker.pos + vec3(0, 1.4f, 0));

    mPlayer.update(dt, mWorld, audio, creatureDist, creatureVisible);

    // --- stalker ---
    bool playerLit = mPlayer.torchOn;
    bool caught = mStalker.update(dt, mWorld, mPlayer.pos, pf, mPlayer.noise, playerLit, audio);

    if (caught && mPlayer.alive) {
        mPlayer.alive = false;
        mState = GS_DEAD;
        mStateTime = 0.0f;
        mDeathTimer = 0.0f;
        mDamage = 1.0f;
        audio.post(SND_DEATH, mPlayer.pos, 1.0f, 1.0f);
        audio.post(SND_SCREECH, mStalker.pos, 1.0f, 0.85f);
        return;
    }

    // --- pickups ---
    mInteractAvailable = false;
    int nearest = -1;
    float nearestD = 1e9f;
    for (size_t i = 0; i < mWorld.pickups.size(); i++) {
        Pickup& p = mWorld.pickups[i];
        if (p.taken) continue;
        p.bob += dt;
        float d = length(vec3(p.pos.x - mPlayer.pos.x, 0, p.pos.z - mPlayer.pos.z));
        if (d < nearestD) { nearestD = d; nearest = (int)i; }
    }
    if (nearest >= 0 && nearestD < 1.7f) {
        mInteractAvailable = true;
        if (mBtnDown[BTN_INTERACT]) {
            mWorld.pickups[nearest].taken = true;
            mBtnDown[BTN_INTERACT] = false;
            mCollected++;
            audio.post(SND_PICKUP, mWorld.pickups[nearest].pos, 0.8f, 1.0f);
            mPlayer.battery = clampf(mPlayer.battery + 0.22f, 0.0f, 1.0f);

            const ChapterSpec& spec = chapterSpec(mChapter);
            int total = (int)mWorld.pickups.size();
            char buf[96];
            if (mCollected >= total) {
                mExitActive = true;
                std::snprintf(buf, sizeof(buf), "ALL %s RECOVERED - FIND THE EXIT", spec.objectiveNoun);
                showMessage(buf, 5.0f);
                audio.post(SND_EXIT_OPEN, mWorld.exitPos, 0.9f, 1.0f);
                // Taking the last one tells it exactly where you are.
                mStalker.lastKnownPlayer = mPlayer.pos;
                mStalker.targetPos = mPlayer.pos;
                mStalker.state = ST_HUNT;
                mStalker.stateTimer = 0.0f;
                mStalker.aggression = std::max(mStalker.aggression, 0.6f);
            } else {
                std::snprintf(buf, sizeof(buf), "%s %d/%d", spec.objectiveNoun, mCollected, total);
                showMessage(buf, 2.5f);
                // Every pickup nudges it toward you.
                mStalker.aggression = clampf(mStalker.aggression + 0.10f, 0.0f, 1.0f);
                if (mStalker.state == ST_WANDER) {
                    mStalker.lastKnownPlayer = mPlayer.pos;
                    mStalker.targetPos = mPlayer.pos;
                    mStalker.state = ST_INVESTIGATE;
                    mStalker.stateTimer = 0.0f;
                    mStalker.repathTimer = 0.0f;
                }
            }
        }
    }

    // --- exit ---
    if (mExitActive) {
        float d = length(vec3(mWorld.exitPos.x - mPlayer.pos.x, 0, mWorld.exitPos.z - mPlayer.pos.z));
        if (d < 1.5f) {
            mState = GS_CHAPTER_DONE;
            mStateTime = 0.0f;
            if (mChapter + 1 >= mUnlocked && mChapter + 1 < CHAPTER_COUNT) {
                mUnlocked = mChapter + 2;
                saveProgress();
            } else if (mChapter + 1 == CHAPTER_COUNT) {
                mUnlocked = CHAPTER_COUNT;
                saveProgress();
            }
            audio.post(SND_DOOR, mWorld.exitPos, 0.9f, 0.8f);
            return;
        }
    }

    updateDirector(dt);

    // --- audio state ---
    audio.setListener(mPlayer.eye(), mPlayer.forward());
    audio.setTension(mPlayer.fear);
    audio.setHeartRate(mPlayer.heartRate());
    float breath = clampf((1.0f - mPlayer.stamina) * 0.85f + mPlayer.fear * 0.4f, 0.0f, 1.0f);
    audio.setBreath(breath);
    audio.setMuffle(clampf((mPlayer.fear - 0.82f) / 0.18f, 0.0f, 1.0f) * 0.45f);
}

// The scare director: paces the non-AI frights so the level never goes quiet
// for too long, and never piles two shocks on top of each other.
void Game::updateDirector(float dt) {
    if (mNearMissCooldown > 0.0f) mNearMissCooldown -= dt;

    // Distant environmental sounds, placed at real positions in the map so they
    // pan and reverberate correctly.
    mAmbientTimer -= dt;
    if (mAmbientTimer <= 0.0f) {
        mAmbientTimer = gRng.range(4.0f, 11.0f);
        for (int tries = 0; tries < 12; tries++) {
            int x = gRng.rangei(1, mWorld.W - 1);
            int z = gRng.rangei(1, mWorld.H - 1);
            if (mWorld.solid(x, z)) continue;
            vec3 c = mWorld.cellCenter(x, z);
            float d = length(c - mPlayer.pos);
            if (d < 6.0f || d > 30.0f) continue;
            float r = gRng.f01();
            if (r < 0.45f) audio.post(SND_DRIP, c, 0.55f, gRng.range(0.85f, 1.2f));
            else if (r < 0.80f) audio.post(SND_METAL, c, 0.40f, gRng.range(0.7f, 1.3f));
            else audio.post(SND_DOOR, c, 0.35f, gRng.range(0.8f, 1.1f));
            break;
        }
    }

    // Whispers, only when it is already near enough to justify them.
    mWhisperTimer -= dt;
    float creatureDist = mStalker.distanceTo(mPlayer.pos);
    if (mWhisperTimer <= 0.0f) {
        mWhisperTimer = gRng.range(16.0f, 34.0f);
        if (creatureDist < 24.0f && mPlayer.fear > 0.25f) {
            // Placed just behind the player. You will turn around. There is
            // nothing there - it is still where the AI says it is.
            vec3 f = mPlayer.forward();
            f.y = 0.0f;
            vec3 behind = mPlayer.pos - normalize(f) * 2.4f + vec3(0, 1.5f, 0);
            audio.post(SND_WHISPER, behind, 0.55f, gRng.range(0.9f, 1.1f));
        }
    }

    // Lights blow out when it is close. Cheap, and it permanently darkens the
    // level as a chapter wears on.
    mLightPopTimer -= dt;
    if (mLightPopTimer <= 0.0f) {
        mLightPopTimer = gRng.range(18.0f, 40.0f);
        if (creatureDist < 20.0f) {
            int best = -1;
            float bestD = 1e9f;
            for (size_t i = 0; i < mWorld.lights.size(); i++) {
                if (!mWorld.lights[i].alive) continue;
                float d = length(mWorld.lights[i].pos - mPlayer.pos);
                if (d < 16.0f && d < bestD) { bestD = d; best = (int)i; }
            }
            if (best >= 0) {
                mWorld.lights[best].alive = false;
                audio.post(SND_LIGHT_POP, mWorld.lights[best].pos, 0.85f, 1.0f);
                mPlayer.addShake(0.35f);
            }
        }
    }

    // Near-miss sting: it passed close behind you without seeing you.
    if (mNearMissCooldown <= 0.0f && creatureDist < 5.5f && mStalker.state != ST_HUNT) {
        vec3 toC = normalize(mStalker.pos - mPlayer.pos);
        vec3 f = mPlayer.forward();
        f.y = 0.0f;
        if (dot(toC, normalize(f)) < -0.3f) {
            audio.post(SND_STINGER, mStalker.pos, 0.5f, 1.25f);
            mPlayer.addShake(0.25f);
            mNearMissCooldown = 25.0f;
        }
    }
}

// -------------------------------------------------------------------- render

void Game::gatherLights(SceneParams& sp) {
    // Nearest eight live lights, with flicker folded into the colour.
    struct Cand { float d; int i; };
    Cand best[MAX_POINT_LIGHTS];
    int n = 0;
    for (size_t i = 0; i < mWorld.lights.size(); i++) {
        const LightSrc& L = mWorld.lights[i];
        if (!L.alive) continue;
        float d = length(L.pos - sp.camPos);
        if (d > L.radius + 26.0f) continue;
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
        sp.lightColor[k] = L.color * (0.55f * clampf(f, 0.0f, 1.0f));
        sp.lightRadius[k] = L.radius;
    }
}

void Game::render() {
    if (!mReady) return;
    const ChapterSpec& spec = chapterSpec(mChapter);

    SceneParams sp;
    sp.time = mTime;
    sp.fogColor = spec.fogColor;
    sp.fogDensity = spec.fogDensity;

    bool inLevel = (mState == GS_PLAY || mState == GS_INTRO ||
                    mState == GS_DEAD || mState == GS_CHAPTER_DONE) && !mWorld.cells.empty();

    if (inLevel) {
        vec3 eye = mPlayer.eye();
        vec3 fwd = mPlayer.forward();
        vec3 up(0, 1, 0);
        if (mState == GS_DEAD) {
            // Camera drops and rolls as you go down, still facing the thing.
            vec3 toC = mStalker.pos + vec3(0, 1.5f, 0) - eye;
            if (lengthSq(toC) > 0.01f) fwd = normalize(lerp(fwd, normalize(toC), clampf(mDeathTimer * 1.6f, 0.0f, 1.0f)));
            float roll = clampf(mDeathTimer * 0.55f, 0.0f, 0.85f);
            up = vec3(std::sin(roll), std::cos(roll), 0);
        } else if (mPlayer.shake > 0.001f) {
            float k = mPlayer.shake * 0.03f;
            fwd = normalize(fwd + vec3(std::sin(mTime * 63.0f) * k, std::cos(mTime * 71.0f) * k, 0));
        }

        sp.camPos = eye;
        sp.view = mat4::lookAt(eye, eye + fwd, up);
        float aspect = (float)mR.width() / (float)std::max(1, mR.height());
        sp.proj = mat4::perspective(72.0f * DEG2RAD, aspect, 0.06f, 90.0f);

        sp.torchPos = eye;
        sp.torchDir = mPlayer.torchDir(mTime);
        float flick = mPlayer.torchFlicker;
        // Proximity interference: the beam stutters when it is close, before
        // you have any other reason to know.
        float cd = mStalker.distanceTo(mPlayer.pos);
        if (cd < 9.0f && mStalker.state != ST_DORMANT) {
            float k = 1.0f - cd / 9.0f;
            flick *= 1.0f - k * 0.45f * (0.5f + 0.5f * std::sin(mTime * 31.0f));
        }
        sp.torchIntensity = mPlayer.torchOn ? 2.05f * clampf(flick, 0.0f, 1.0f) : 0.0f;
        sp.torchRange = 20.0f;
        sp.torchColor = vec3(1.0f, 0.93f, 0.80f);
        sp.ambient = vec3(0.018f, 0.019f, 0.024f);
        gatherLights(sp);

        // --- shadow pass ---
        if (sp.torchIntensity > 0.01f) {
            mR.beginShadowPass(sp);
            mR.drawShadow(mWorldMesh, mat4());
            mR.drawShadow(mPropMesh, mat4());
            for (int i = 0; i < BP_COUNT; i++)
                mR.drawShadow(mStalkerParts[i], mStalker.parts[i]);
            mR.endShadowPass();
        } else {
            mR.beginShadowPass(sp);
            mR.endShadowPass();
        }

        // --- scene ---
        mR.beginScene(sp);
        mR.draw(mWorldMesh, mat4(), spec.wallTint, TEX_WALL, TEX_FLOOR, 1.0f, 0.0f);
        mR.draw(mPropMesh, mat4(), vec3(0.62f, 0.60f, 0.58f), TEX_METAL, TEX_METAL, 0.0f, 0.0f);

        if (mStalker.state != ST_DORMANT || mState == GS_DEAD) {
            vec3 tint(0.80f, 0.76f, 0.74f);
            for (int i = 0; i < BP_COUNT; i++)
                mR.draw(mStalkerParts[i], mStalker.parts[i], tint, TEX_FLESH, TEX_FLESH, 0.0f, 0.015f);
        }

        for (const Pickup& p : mWorld.pickups) {
            if (p.taken) continue;
            float bob = std::sin(p.bob * 1.7f) * 0.06f;
            mat4 xf = mat4::translation(p.pos + vec3(0, bob, 0)) * mat4::rotationY(p.bob * 0.9f);
            mR.draw(mPickupMesh, xf, vec3(0.85f, 0.88f, 0.70f), TEX_METAL, TEX_METAL, 0.0f, 0.42f);
        }

        {
            float em = mExitActive ? (0.35f + 0.18f * std::sin(mTime * 2.4f)) : 0.02f;
            vec3 tint = mExitActive ? vec3(0.55f, 0.95f, 0.62f) : vec3(0.45f, 0.45f, 0.48f);
            mR.draw(mExitMesh, mat4::translation(mWorld.exitPos), tint, TEX_METAL, TEX_METAL, 0.0f, em);
        }

        mR.endScene();
        mR.postProcess(mPlayer.fear, mTime, mFade, mDamage);
    } else {
        // Title / series-complete: no world, just the vignette over black.
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

    mR.uiTextCentered("HOLLOW SIGNAL", w * 0.5f, h * 0.16f, 54.0f * s, 0.86f, 0.84f, 0.80f, pulse);
    mR.uiTextCentered("A HORROR SERIES", w * 0.5f, h * 0.16f + 74.0f * s, 18.0f * s,
                      0.42f, 0.44f, 0.46f, 0.9f);
    mR.uiQuad(w * 0.5f - 150.0f * s, h * 0.16f + 108.0f * s, 300.0f * s, 1.5f * s,
              0.35f, 0.36f, 0.38f, 0.7f);

    float y0 = h * 0.42f;
    for (int i = 0; i < CHAPTER_COUNT; i++) {
        const ChapterSpec& c = chapterSpec(i);
        bool unlocked = i < mUnlocked;
        float a = unlocked ? 0.95f : 0.28f;
        char line[96];
        std::snprintf(line, sizeof(line), "%d.  %s", i + 1, unlocked ? c.title : "LOCKED");
        mR.uiTextCentered(line, w * 0.5f, y0 + i * 58.0f * s, 26.0f * s,
                          unlocked ? 0.82f : 0.5f, unlocked ? 0.80f : 0.5f, 0.76f, a);
        if (unlocked) {
            mR.uiTextCentered(c.subtitle, w * 0.5f, y0 + i * 58.0f * s + 28.0f * s, 13.0f * s,
                              0.40f, 0.42f, 0.44f, 0.8f);
        }
    }

    mR.uiTextCentered("TAP AN EPISODE TO BEGIN", w * 0.5f, h - 84.0f * s, 16.0f * s,
                      0.55f, 0.55f, 0.55f, 0.5f + 0.3f * std::sin(mTime * 2.2f));
    mR.uiTextCentered("HEADPHONES RECOMMENDED", w * 0.5f, h - 52.0f * s, 13.0f * s,
                      0.38f, 0.38f, 0.40f, 0.7f);
}

void Game::renderHud() {
    float s = mUiScale;
    float h = (float)mR.height();
    const ChapterSpec& spec = chapterSpec(mChapter);

    // Objective counter.
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s  %d/%d", spec.objectiveNoun,
                  mCollected, (int)mWorld.pickups.size());
    mR.uiText(buf, 26.0f * s, 26.0f * s, 20.0f * s, 0.80f, 0.78f, 0.74f, 0.85f);

    // Torch battery.
    float bx = 26.0f * s, by = h - 56.0f * s, bw = 190.0f * s, bh = 9.0f * s;
    mR.uiQuad(bx, by, bw, bh, 0.10f, 0.10f, 0.11f, 0.65f);
    float batC = mPlayer.battery < 0.25f ? 1.0f : 0.85f;
    mR.uiQuad(bx, by, bw * mPlayer.battery, bh,
              batC, 0.82f * (mPlayer.battery < 0.25f ? 0.4f : 1.0f), 0.45f, 0.9f);
    mR.uiText("TORCH", bx, by - 22.0f * s, 13.0f * s, 0.55f, 0.55f, 0.55f, 0.7f);

    // Stamina, only while it matters.
    float staAlpha = (mPlayer.stamina < 0.999f) ? 0.9f : 0.0f;
    if (staAlpha > 0.0f) {
        float sy = by - 42.0f * s;
        mR.uiQuad(bx, sy, bw, bh * 0.7f, 0.10f, 0.10f, 0.11f, 0.55f * staAlpha);
        mR.uiQuad(bx, sy, bw * mPlayer.stamina, bh * 0.7f, 0.55f, 0.72f, 0.85f, 0.8f * staAlpha);
    }

    // Movement stick: drawn only where the thumb actually is.
    if (mStickTouch >= 0 && mTouch[mStickTouch].active) {
        Touch& t = mTouch[mStickTouch];
        float maxR = 90.0f * s;
        mR.uiRing(t.startX, t.startY, maxR, 2.5f * s, 0.75f, 0.75f, 0.78f, 0.22f);
        float dx = t.x - t.startX, dy = t.y - t.startY;
        float d = std::sqrt(dx * dx + dy * dy);
        if (d > maxR) { dx = dx / d * maxR; dy = dy / d * maxR; }
        mR.uiDisc(t.startX + dx, t.startY + dy, 30.0f * s, 0.85f, 0.85f, 0.88f, 0.30f);
    }

    // Action buttons.
    struct { int id; const char* label; } btns[3] = {
        {BTN_SPRINT, "RUN"}, {BTN_CROUCH, "CROUCH"}, {BTN_TORCH, "LIGHT"}
    };
    for (int i = 0; i < 3; i++) {
        int b = btns[i].id;
        bool down = mBtnDown[b];
        float a = down ? 0.42f : 0.20f;
        bool disabled = (b == BTN_SPRINT && mPlayer.stamina < 0.06f) ||
                        (b == BTN_TORCH && mPlayer.battery <= 0.0f);
        if (disabled) a *= 0.4f;
        mR.uiDisc(mBtnX[b], mBtnY[b], mBtnR[b], 0.80f, 0.80f, 0.84f, a);
        mR.uiRing(mBtnX[b], mBtnY[b], mBtnR[b], 2.0f * s, 0.85f, 0.85f, 0.88f, a + 0.15f);
        float tw = mR.textWidth(btns[i].label, 14.0f * s);
        mR.uiText(btns[i].label, mBtnX[b] - tw * 0.5f, mBtnY[b] - 7.0f * s, 14.0f * s,
                  0.92f, 0.92f, 0.94f, disabled ? 0.35f : 0.85f);
    }

    if (mInteractAvailable) {
        float pulse = 0.55f + 0.25f * std::sin(mTime * 5.0f);
        int b = BTN_INTERACT;
        mR.uiDisc(mBtnX[b], mBtnY[b], mBtnR[b], 0.85f, 0.86f, 0.62f, 0.22f * pulse * 2.0f);
        mR.uiRing(mBtnX[b], mBtnY[b], mBtnR[b], 2.5f * s, 0.90f, 0.90f, 0.66f, pulse);
        float tw = mR.textWidth("TAKE", 16.0f * s);
        mR.uiText("TAKE", mBtnX[b] - tw * 0.5f, mBtnY[b] - 8.0f * s, 16.0f * s,
                  0.95f, 0.95f, 0.80f, 0.95f);
    }

    // Exit direction hint, only once the exit is live and only as a faint chevron.
    if (mExitActive) {
        vec3 d = mWorld.exitPos - mPlayer.pos;
        d.y = 0.0f;
        float dist = length(d);
        std::snprintf(buf, sizeof(buf), "EXIT  %dM", (int)dist);
        mR.uiText(buf, 26.0f * s, 54.0f * s, 16.0f * s, 0.55f, 0.92f, 0.60f, 0.75f);
    }
}

void Game::renderOverlayText() {
    float s = mUiScale;
    float w = (float)mR.width(), h = (float)mR.height();
    const ChapterSpec& spec = chapterSpec(mChapter);

    if (mState == GS_INTRO) {
        float a = clampf(mStateTime / 0.9f, 0.0f, 1.0f) * clampf((4.2f - mStateTime) / 0.8f, 0.0f, 1.0f);
        mR.uiTextCentered(spec.subtitle, w * 0.5f, h * 0.40f, 16.0f * s, 0.45f, 0.46f, 0.48f, a);
        mR.uiTextCentered(spec.title, w * 0.5f, h * 0.40f + 34.0f * s, 42.0f * s, 0.85f, 0.83f, 0.79f, a);
        char sub[128];
        std::snprintf(sub, sizeof(sub), "RECOVER %d %s AND GET OUT",
                      (int)mWorld.pickups.size(), spec.objectiveNoun);
        mR.uiTextCentered(sub, w * 0.5f, h * 0.40f + 96.0f * s, 15.0f * s, 0.50f, 0.51f, 0.52f, a);
    } else if (mState == GS_DEAD) {
        float a = clampf((mStateTime - 1.8f) / 1.0f, 0.0f, 1.0f);
        mR.uiTextCentered("IT FOUND YOU", w * 0.5f, h * 0.44f, 40.0f * s, 0.72f, 0.16f, 0.14f, a);
        if (mStateTime > 2.6f) {
            mR.uiTextCentered("TAP TO TRY AGAIN", w * 0.5f, h * 0.44f + 62.0f * s, 16.0f * s,
                              0.55f, 0.55f, 0.55f, a * (0.55f + 0.35f * std::sin(mTime * 3.0f)));
        }
    } else if (mState == GS_CHAPTER_DONE) {
        float a = clampf(mStateTime / 1.0f, 0.0f, 1.0f);
        mR.uiTextCentered("YOU GOT OUT", w * 0.5f, h * 0.42f, 38.0f * s, 0.80f, 0.80f, 0.76f, a);
        mR.uiTextCentered(spec.title, w * 0.5f, h * 0.42f + 52.0f * s, 18.0f * s, 0.48f, 0.50f, 0.50f, a);
        if (mStateTime > 2.2f) {
            const char* next = (mChapter + 1 < CHAPTER_COUNT) ? "TAP FOR THE NEXT EPISODE" : "TAP TO CONTINUE";
            mR.uiTextCentered(next, w * 0.5f, h * 0.42f + 96.0f * s, 16.0f * s,
                              0.55f, 0.55f, 0.55f, a * (0.55f + 0.35f * std::sin(mTime * 3.0f)));
        }
    } else if (mState == GS_SERIES_DONE) {
        float a = clampf(mStateTime / 1.4f, 0.0f, 1.0f);
        mR.uiTextCentered("THE SIGNAL STOPS", w * 0.5f, h * 0.36f, 40.0f * s, 0.84f, 0.82f, 0.78f, a);
        mR.uiTextCentered("YOU REACHED THE SURFACE", w * 0.5f, h * 0.36f + 56.0f * s, 18.0f * s,
                          0.55f, 0.56f, 0.56f, a);
        mR.uiTextCentered("IT DID NOT FOLLOW", w * 0.5f, h * 0.36f + 84.0f * s, 18.0f * s,
                          0.55f, 0.56f, 0.56f, a * clampf(mStateTime - 2.0f, 0.0f, 1.0f));
        mR.uiTextCentered("THAT IS WHAT WORRIES YOU", w * 0.5f, h * 0.36f + 112.0f * s, 18.0f * s,
                          0.50f, 0.40f, 0.40f, a * clampf(mStateTime - 3.6f, 0.0f, 1.0f));
        if (mStateTime > 5.0f) {
            mR.uiTextCentered("TAP TO RETURN", w * 0.5f, h - 90.0f * s, 15.0f * s,
                              0.5f, 0.5f, 0.5f, 0.5f + 0.3f * std::sin(mTime * 2.5f));
        }
    }

    if (mMessageTime > 0.0f && mState == GS_PLAY) {
        float a = clampf(mMessageTime, 0.0f, 1.0f);
        mR.uiTextCentered(mMessage.c_str(), w * 0.5f, h * 0.72f, 19.0f * s,
                          0.82f, 0.80f, 0.72f, a * 0.9f);
    }
}

} // namespace hm
