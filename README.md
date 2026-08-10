# HOLLOW SIGNAL

A first-person horror game for Android, written in C and C++ against the NDK.
Five episodes, real-time 3D, and a stalker that hunts you by sound.

**Prebuilt APK: [`dist/HollowSignal-1.0.apk`](dist/HollowSignal-1.0.apk)** — 845 KB,
signed, arm64-v8a + armeabi-v7a, Android 5.0 (API 21) and up.

---

## What it is

You are underground with a torch and a failing battery, looking for the pieces
of something that will let you leave. There is one other thing down there. It
is not scripted, it does not spawn on triggers, and it does not chase you the
whole time — which is exactly why it works.

Five episodes, each a bigger and darker map than the last, unlocked in order
and saved between sessions:

| # | Episode | Objective | Map |
|---|---------|-----------|-----|
| 1 | The Basement | 4 fuses | 25×25 |
| 2 | The Ward | 5 charts | 29×29 |
| 3 | Sublevel C | 5 cells | 31×31 |
| 4 | The Tunnels | 6 valves | 35×35 |
| 5 | The Broadcast | 6 relays | 37×37 |

### Controls

Left half of the screen is a floating movement stick — put your thumb anywhere.
Right half is look. Buttons bottom-right: **RUN**, **CROUCH**, **LIGHT**. A
**TAKE** button appears when you are next to an objective. Back button returns
to the episode list.

---

## What makes it frightening

The scares are systemic rather than placed, because placed scares stop working
the second time you walk down the same corridor.

**Sound is the monster's primary sense.** Sprinting is audible from about
twelve metres, walking from four, crouch-walking from barely one. This makes
the run button a real decision instead of a free escape, and it is measurable:
in the automated playtests, a sprinting player has the stalker actively on them
**60% of the time**, a crouching player **29%**, and gets found **13 seconds
sooner** on average.

**It circles instead of charging.** After it investigates a noise and finds
nothing, it does not give up — it moves to a holding position seven to thirteen
metres away, preferring cells you cannot see and your blind side. It growls
occasionally from there. This is the state the game spends most of its time in,
and it is the one that actually generates dread.

**It gives up if you earn it.** Stay quiet for long enough and it loses
interest and goes back to patrol. Without that, a chapter flattens into one
unbroken chase and the quiet stretches — the ones that make the loud parts
land — never happen.

**A director clock guarantees the encounter.** Purely emergent patrol is
luck-based: on the larger maps the stalker can spend four minutes in a wing you
never enter. A timer ensures it eventually comes looking, and that timer runs
faster the louder you have been and the deeper into the episode you are. So
the guarantee doubles as the stealth reward.

**Your torch is the light and the liability.** It is the only shadow-casting
light in the scene, so the creature's silhouette lands on the wall behind it —
often before you have consciously worked out what you are looking at. It also
makes you visible from much further away. The beam stutters when the creature
is within nine metres, which tells you something is wrong before anything else
does. The battery drains the whole time.

**Escalating wrongness.** Fear rises with proximity and sight, and drives the
heartbeat rate, breathing, vignette, chromatic aberration, desaturation, a
breathing lens warp, and — above 55% — occasional single-frame horizontal
tearing. Players tend to report that something felt wrong rather than that they
saw an effect.

**Lights blow out permanently** when it is near, so a map gets darker the
longer you are in it.

### The creature

Original design, deliberately not a costume: proportions that read as human for
about half a second and then stop. Forearms longer than the upper arms, arms
that hang past the knees, a head on too long a neck, and no face at all —
nothing to read an intention off, which is worse than teeth. The right leg
drags; the limp is the cheapest thing that makes a walk cycle feel wrong. It
lolls its head while patrolling and snaps it level when it commits.

Eleven rigid parts posed by forward kinematics — cheaper than skinning and
indistinguishable at torch range.

---

## How it is built

Pure C++17 on the NDK via `NativeActivity`. No Java game code at all
(`android:hasCode="false"`); the only `classes.dex` in the APK is the stub the
build system generates.

```
app/src/main/cpp/
  main.cpp      EGL setup, lifecycle, multi-touch input pump
  game.cpp/h    state machine, HUD, touch handling, scare director
  gfx.cpp/h     GLES 3.0 renderer, shadow mapping, post-processing
  world.cpp/h   maze generation, collision, line of sight, nav fields
  entity.cpp/h  the stalker: senses, state machine, procedural animation
  player.cpp/h  first-person controller, torch, stamina, fear
  audio.cpp/h   real-time procedural synthesis + reverb, OpenSL ES backend
  mesh.h        geometry builders
  noise.h       value noise / fbm / worley
  hmath.h       vectors, matrices, PRNG
  font.h        5×7 bitmap font
```

### Everything is generated at runtime

There is not a single texture, model, or sound file in this repository, and
that is a design decision rather than a shortcut.

**Levels** are a recursive-backtracker maze with rooms carved through it and
extra loops punched in — a perfect maze is tedious rather than frightening,
because you always know the way back and nothing can come from behind you.
Objectives are placed one per distance band from spawn, so you are forced to
cross the whole map.

**Textures** are baked at load: concrete with cast marks and damp streaks
running down it, floor tiles with grime pooling in the joints, brushed metal
with rust blooming out of the low spots, and the creature's skin from worley
cells with veins threaded through it.

**Every sound is synthesised sample by sample on the audio thread.** Footsteps
are a thump plus grit through a bandpass. The creature's step is a sub-bass
body with a dragged scrape. The screech is FM with a high modulation index,
swept and clipped. Whispers are noise through a sliding formant pair gated by
a syllable envelope. Pickups are a minor second rather than a major third, so
they read as wrong rather than as a reward. It all runs through a Schroeder
reverb — four damped combs into two allpasses — which is what makes the
corridors sound like concrete instead of like a phone speaker.

### Performance

The 3D scene renders to an offscreen buffer that the post pass upscales. A
governor watches the framerate and drops that buffer's resolution if the device
cannot hold 40 fps, so weak hardware loses sharpness instead of losing
smoothness. Everything is drawn in a handful of calls: the world is one mesh,
all props are baked into a second, and the creature is eleven small ones.
Between two and eleven thousand triangles per level.

---

## Building it yourself

Requires the Android SDK with NDK 26.1.10909125 and CMake 3.22.1.

```sh
echo "sdk.dir=/path/to/android-sdk" > local.properties
gradle :app:assembleRelease
```

The output is unsigned. To sign it with your own key:

```sh
keytool -genkeypair -keystore my-key.jks -alias mykey -keyalg RSA \
        -keysize 2048 -validity 10000
$SDK/build-tools/34.0.0/zipalign -p -f 4 \
    app/build/outputs/apk/release/app-release-unsigned.apk aligned.apk
$SDK/build-tools/34.0.0/apksigner sign --ks my-key.jks \
    --min-sdk-version 21 --out HollowSignal.apk aligned.apk
```

The APK in `dist/` is signed with a throwaway key that is deliberately not in
this repository. **Generate your own before publishing anywhere** — you cannot
change an app's signing key after release.

## Tests

```sh
./tests/run_tests.sh        # 167 checks on the platform-independent code
python3 tests/check_shaders.py   # compiles all 8 GLSL shaders
```

`tests/run_tests.sh` compiles the level generation, navigation, AI, player
physics and audio DSP for the host machine and exercises them headlessly. It
checks that every objective and the exit are reachable from spawn, that the map
border is sealed, that no prop is buried in a wall, that nav fields cover every
reachable cell, that meshes fit in 16-bit indices with unit normals, that line
of sight is symmetric and does not leak through corners, that neither the
player nor the stalker can end up inside geometry, that the mixer never emits a
non-finite sample or runs away, that the skeleton assembles into something
person-shaped, and that four minutes of simulated play per episode produces a
stalker that patrols, investigates, stalks, hunts, searches, and kills.

These caught four genuine bugs during development: line of sight leaking
diagonally between wall blocks so the creature could see through a corner you
could not; collision resolution leaving entities inside walls on large steps;
the stalker being able to spend an entire episode in a wing you never visit;
and — the one that mattered most — the original stealth mechanic being
statistically almost meaningless until the director clock was tied to how much
noise you had been making.

### What is not tested

**The game has never been run on a device or an emulator.** There was no
Android device and no GPU available in the environment it was built in. The
level generation, AI, physics and audio DSP are verified by the suite above,
and all eight shaders compile under `glslangValidator`, but the renderer's
actual output, the touch controls in the hand, and the real-world framerate are
unverified. Play it before you show it to anyone.

## Notes

**On the APK being 845 KB rather than 100–300 MB.** File size in a game is
almost entirely audio and texture data. This one generates all of it at
runtime, so there is nothing to ship. The size could be raised by including
recorded audio and authored textures instead of synthesising them — that would
be a real improvement to the sound and the surfaces, not just weight — but
padding the file to hit a number would make it slower to download and identical
to play.

**On real people.** This uses an original creature and no real person's name,
likeness, or image. Building a horror game around identifiable living people
means using their likeness commercially without consent, which is a genuine
legal exposure and not something worth doing to a real person regardless.

## Licence

See [LICENSE](LICENSE).
