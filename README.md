# HOLLOW SIGNAL

An open-world first-person horror game for Android. One contiguous 320m city
district, sixteen missions, three kinds of monster, survivors to find, and a
synthesised voice on the other end of the radio.

Written in **C++** (engine), **C** (asset-pack validator, NDK glue),
**Kotlin** (app shell, lifecycle, audio focus, save mirror) and **Java**
(haptics, device tiering).

**Prebuilt APK: [`dist/HollowSignal-2.1.apk`](dist/HollowSignal-2.1.apk)** —
64 MB, signed, arm64-v8a + armeabi-v7a, Android 5.0 (API 21) and up.

---

## The game

You are on foot in a dead city district at night with a torch, a failing
battery, three flares and a radio. Dispatch gives you jobs. Between them you
can walk anywhere — there are no level boundaries and no loading screens; the
buildings you can enter are carved out of the same grid the street is on.

**Sixteen missions**, unlocked in order, saved between sessions:

| # | Job | Type | # | Job | Type |
|---|-----|------|---|-----|------|
| 01 | Contact | reach | 09 | The Tunnels | reach |
| 02 | Supplies | collect ×3 | 10 | Relay One | activate ×3 |
| 03 | The Substation | activate ×2 | 11 | The Ward | rescue ×2 |
| 04 | First Light | reach | 12 | Blackout | survive 75s |
| 05 | The Clinic | rescue | 13 | Last Cache | collect ×5 |
| 06 | Walk Them Home | escort | 14 | The Roof | reach |
| 07 | Scavenge | collect ×4 | 15 | Broadcast | activate ×4 |
| 08 | Hold Position | survive 60s | 16 | The Harbour | escape |

Each job raises the monster pressure: how many are awake, what kinds, and how
fast they lose patience. Dying restarts the current job, not the game.

### Controls

Left half of the screen is a floating movement stick — thumb anywhere. Right
half is look. Bottom right: **RUN**, **CROUCH**, **LIGHT**, **FLARE**. A
context button appears when you can **TAKE**, **HOLD** or **HELP**. Compass
strip along the top, rotating minimap top right. Back returns to the title.

---

## What makes it frightening

The scares are systemic rather than placed, because a placed scare stops
working the second time down the same street.

**Sound is the primary sense.** Sprinting is audible from about sixteen
metres, walking from six, crouch-walking from barely one. This is measurable
rather than aspirational: in the automated playtests a sprinting player has a
monster actively on them **56% of the time and is first found after 20
seconds**; crouching drops that to **34% and 44 seconds**.

**Three monsters that want different things.**

- The **Stalker** is tall, patient and hunts by sound. After investigating a
  noise and finding nothing it does not give up — it takes a holding position
  eight to fifteen metres away, preferring cells you cannot see and your blind
  side, and growls from there. This is the state the game spends most of its
  time in and the one that actually generates dread.
- The **Crawler** is low, fast and nearly deaf. It will not find you by
  listening; it finds you by blundering into you, and then it is much quicker
  than you are.
- The **Watcher** never moves. It stands in one place, sees much further than
  anything else, and when it spots you it screams — and every other monster on
  the map is handed your position. It cannot hurt you. It does not need to.

**It gives up if you earn it.** Stay quiet long enough and the stalker loses
interest and returns to patrol. Without that, a session flattens into one
unbroken chase and the quiet stretches — the ones that make the loud parts
land — never happen.

**A director clock guarantees the encounter.** Purely emergent patrol is
luck-based on a map this size; a monster can spend an entire mission in a
block you never enter, and an encounter that never happens is not tension, it
is an empty city. A timer ensures one eventually comes looking, and that timer
runs far faster the louder you have been. The guarantee doubles as the stealth
reward.

**Brightness is yours to set.** The title screen has a calibration strip and a
brightness control, because "as dark as it should be" depends entirely on the
screen and the room. The night is lit by a weak moonlight term so the city has
readable silhouettes outside the torch cone; interiors are much darker, which
is what makes stepping through a door land.

**Your torch is the light and the liability.** It is the only shadow-casting
light in the scene, so a monster's silhouette lands on the wall behind it,
usually before you have consciously worked out what you are looking at. It
also makes you visible from much further away, and the beam stutters when
something is within nine metres — which tells you before anything else does.

**Flares buy distance, never a kill.** A flare stuns everything that can see
it for a few seconds. There is no winning a fight here, only choosing when it
happens.

**Survivors can die.** Escort targets are killed by monsters like anything
else, which makes walking one across four blocks a real risk rather than a
followed waypoint.

**Screams that are built to the measured signature.** Human screams occupy a
band nothing else does: amplitude modulation between 30 and 150Hz, which
[Arnal et al. (Current Biology, 2015)](https://www.cell.com/fulltext/S0960-9822(15)00737-X)
showed reaches the amygdala over a shorter path than the auditory cortex, and
which listeners rate as more frightening the rougher it gets. Ordinary speech
modulates at 4-5Hz and never enters it. Every cry in the game applies that
modulation explicitly, and the tests measure it rather than assume it: the
scream reads 0.71, the monster wail 0.81, and a spoken radio line 0.08.

**Creaks that are a physical model, not filtered noise.** Two loaded surfaces
do not slide smoothly — they stick while elastic strain builds, release in a
sudden slip, and stick again. `tools/bake.cpp` simulates that relaxation
oscillation directly and rings a bank of resonant modes with each slip, so a
door pushed slowly grinds out individual grains and the same door pushed fast
squeals, from one model. Doors creak when you cross a threshold, floorboards
give under you indoors every few paces, hinges and structural groans fill the
ambient mix.

**Escalating wrongness.** Fear drives heartbeat rate, breathing, vignette,
chromatic aberration, desaturation, a breathing lens warp and — above 55% —
occasional single-frame horizontal tearing. Players tend to report that
something felt wrong rather than that they saw an effect.

---

## Voices

Every spoken line is generated by a formant speech synthesiser built for this
project: a Rosenberg glottal source (or noise, for unvoiced sounds) driven
through a cascade of five resonators tracking per-phoneme formant targets,
with lip radiation, jitter, shimmer, coarticulated formant glides and a pitch
contour that declines across the utterance and lifts on stressed syllables.

**These are synthesised voices, not recordings of people.** No human was
recorded and no voice acting was licensed. Every line is then run through a
radio channel — band-limited to 300–3400Hz, compressed, clipped, with carrier
hiss, dropouts and squelch — which is a design choice rather than a disguise:
a handheld radio is precisely the context where a slightly synthetic voice
reads as correct. Survivors in the room are processed much more lightly.

The vowels are acoustically correct, and the tests prove it rather than assert
it: `tests/voice_test.cpp` measures the harmonic envelope of every sustained
vowel and checks each formant lands on its target frequency.

Non-verbal vocalisations — screams, gasps, sobbing, the monsters' roars and
chittering — come off the same vocal tract with the jitter turned up until the
folds stop being periodic. That is what separates a shout from a scream, and a
scream from something that does not have human vocal folds at all.

Every line is subtitled, always.

---

## How it is built

```
app/src/main/
  cpp/                    the engine
    main.cpp              EGL, lifecycle, multi-touch input pump
    game.cpp/h            state machine, streaming, HUD, scare director
    gfx.cpp/h             GLES 3.0: texture arrays, normal mapping, shadows
    world.cpp/h           city generation, interiors, collision, sight, nav
    actors.cpp/h          three monsters and the survivors, one shared rig
    missions.cpp/h        the sixteen-job chain
    player.cpp/h          controller, torch, stamina, fear
    audio.cpp/h           mixer: procedural SFX + baked speech and ambience
    speech.cpp/h          the formant synthesiser
    assets.cpp/h          asset-pack mapping
    pak_verify.c          pack validation (C)
    jnibridge.cpp         the JNI edge
  kotlin/                 MainActivity, Progress
  java/                   Haptics, DeviceProfile
tools/
  materials.cpp           22 procedural materials
  bake.cpp                the offline asset build
tests/
  host_test.cpp           177 checks on the platform-independent code
  voice_test.cpp          55 checks on the synthesiser
  check_shaders.py        compiles all 8 GLSL shaders
```

### The asset pipeline

`tools/bake.sh` generates every texture and every recorded-style sound and
packs them into `app/src/main/assets/hollow.pak` (61 MB, deterministic). It is
stored **uncompressed** inside the APK so the runtime can memory-map it and
hand pixel data straight to the driver — no inflate step, no second copy. That
costs download size and buys load time.

**22 materials**, each authored procedurally at 512×512 as an albedo map plus
a tangent-space normal map with roughness in alpha, derived by Sobel from a
generated height field. Not noise: brick has a running bond with mortar
courses, chamfered edges and efflorescence; concrete has form-board marks and
tie holes; tile has grout that collects the dirt; corrugated steel has ribs
that curve the normal and rust blooming from where water sits; wood has grain
along the plank and knots. Damp runs downward on every vertical surface,
because that is what rain does.

**Audio**: 33 voice lines, 24 vocalisation takes and three 50-second stereo
ambience beds. The beds are baked long specifically so they never audibly
loop, which short loops always eventually do.

### Rendering

Forward lighting, GLES 3.0. One shadow-casting spotlight (your torch) with 3×3
PCF, up to eight point lights, exponential-squared fog, and per-material
roughness driving specular sharpness so wet asphalt catches the beam and
plaster does not. Baked corner occlusion darkens the ambient term.

Every material is a layer of one texture array, so an entire chunk of city —
road, kerb, brick, glass, steel — is a single draw call. The city meshes into
49 chunks of about 1,500 triangles each, culled by frustum and distance;
71,000 triangles total, of which only a fraction is submitted per frame.

The 3D scene renders to an offscreen buffer that the post pass upscales.
`DeviceProfile.java` picks a starting resolution from RAM, core count and GL
version before the first frame, and a framerate governor adapts from there —
so weak hardware loses sharpness rather than smoothness, and never renders a
frame it cannot afford.

---

## Building it yourself

Requires the Android SDK with NDK 26.1.10909125 and CMake 3.22.1, plus clang
for the asset bake.

```sh
echo "sdk.dir=/path/to/android-sdk" > local.properties
./tools/bake.sh                       # generates assets/hollow.pak (~2 min)
gradle :app:assembleRelease
```

The output is unsigned. To sign with your own key:

```sh
keytool -genkeypair -keystore my-key.jks -alias mykey -keyalg RSA \
        -keysize 2048 -validity 10000
$SDK/build-tools/34.0.0/zipalign -p -f 4 \
    app/build/outputs/apk/release/app-release-unsigned.apk aligned.apk
$SDK/build-tools/34.0.0/apksigner sign --ks my-key.jks \
    --min-sdk-version 21 --out HollowSignal.apk aligned.apk
```

The APK in `dist/` is signed with a throwaway key that is deliberately not in
this repository. **Generate your own before publishing anywhere** — an app's
signing key cannot be changed after release.

## Tests

```sh
./tests/run_tests.sh          # 61 voice + 11 render + 177 engine checks
python3 tests/check_shaders.py
```

`tests/render_test.cpp` renders actual frames through desktop Mesa on an EGL
pbuffer and inspects the pixels. It cannot say anything about performance on a
phone, but it answers the question the other tests structurally cannot: is
there a picture? It exists because version 2.0 shipped a completely black
screen that every simulation test passed straight through.

The suite builds the city, navigation, AI, physics, missions, audio DSP and
asset loading for the host machine and exercises them headlessly. It checks
that **every walkable cell in the district is reachable on foot**, that every
enterable building has a reachable door, that every mission objective and
drop-off can actually be walked to, that chunk meshes have unit normals and
tangents that are not parallel to them, that every material named in
`matids.h` exists in the pack and every mission's briefing audio is present,
that line of sight is symmetric and never leaks diagonally through a corner,
that nothing ever ends up inside a wall, that a stunned monster cannot reach
you and does recover, that survivors follow, flee and stay out of walls, that
the mixer never emits a non-finite sample, that the pack reader rejects
corrupt input, and that four minutes of simulated play per monster type
produces the behaviour the design calls for.

These caught eleven genuine bugs. The most instructive:

- **Three separate causes of the black screen in 2.0**, none of which any
  simulation test could see. The material texture arrays were allocated with
  one mip level but filtered `LINEAR_MIPMAP_LINEAR`, leaving them
  mipmap-incomplete — and GLES samples an incomplete texture as opaque black.
  The vignette used `smoothstep(1.05, 0.26, d)`, which GLSL explicitly leaves
  undefined when `edge0 >= edge1`. And the film-grain hash multiplied UVs up to
  ~450,000 inside a `mediump` shader, where mobile fp16 tops out at 65504 — it
  overflowed to infinity, `fract(infinity)` returned NaN, and the NaN
  propagated into every pixel. Desktop drivers that quietly promote `mediump`
  to fp32 render that last one correctly, which is exactly what makes it easy
  to ship.

- The speech synthesiser was missing the **lip radiation** term, so low
  frequencies swamped the formants and every vowel measured identically.
- Buildings on the district edge put their only door on row 0, which was then
  sealed — walling **406 interior cells** behind a door that opened into rock.
- Line of sight leaked diagonally between wall blocks, so a monster could see
  through a corner you could not.
- The Watcher's legs were shorter than its hip height, so it floated 80cm off
  the ground.
- Stealth was statistically nearly meaningless until the director clock was
  tied to how much noise the player had been making.

Two of the test methods were themselves wrong before they were right, and the
comments in the test files record why: a random-walk bot that never left the
spawn made the AI look broken, and both raw spectral peak-picking and LPC gave
false readings on the vowels.

### What is not tested

**The game has never been run on a device or an emulator.** There was no
Android device and no GPU in the environment it was built in. Everything above
is verified, and all eight shaders compile under `glslangValidator`, but the
renderer's actual output, the feel of the controls, and the real framerate are
unverified. Play it before showing it to anyone.

## Notes

**On real people.** This uses original creatures and characters. No real
person's name, likeness or image appears anywhere in it.

## Licence

See [LICENSE](LICENSE).
