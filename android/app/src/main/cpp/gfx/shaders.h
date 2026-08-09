// All GLSL, embedded. Compiled with `#version 300 es` plus per-variant defines.
#pragma once

namespace hl {
namespace shaders {

// ------------------------------------------------------------------ scene
//
// One forward pass. The torch is a shadow-casting spotlight; everything else is
// a handful of unshadowed point lights plus a very dark ambient. Materials are
// metal-rough PBR, sampled either through UVs (architecture, props) or by
// triplanar projection (creatures, which have no sensible unwrap).

inline const char* scene_vs = R"GLSL(
precision highp float;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec4 aTan;
layout(location = 3) in vec2 aUV;
#ifdef SKINNED
layout(location = 4) in uvec4 aBone;
layout(location = 5) in vec4 aWeight;
uniform mat4 uBones[24];
#endif

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat4 uLightViewProj;

out vec3 vWorld;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vUV;
out vec4 vLightPos;

void main() {
    vec4 p = vec4(aPos, 1.0);
    vec3 n = aNrm;
    vec3 t = aTan.xyz;

#ifdef SKINNED
    mat4 skin =
        uBones[aBone.x] * aWeight.x +
        uBones[aBone.y] * aWeight.y +
        uBones[aBone.z] * aWeight.z +
        uBones[aBone.w] * aWeight.w;
    p = skin * p;
    // Skinning matrices here are rigid (rotation + translation), so the upper
    // 3x3 transforms normals correctly without an inverse-transpose.
    n = mat3(skin) * n;
    t = mat3(skin) * t;
#endif

    vec4 world = uModel * p;
    vWorld = world.xyz;
    mat3 nm = mat3(uModel);
    vNormal = normalize(nm * n);
    vTangent = vec4(normalize(nm * t), aTan.w);
    vUV = aUV;
    vLightPos = uLightViewProj * world;
    gl_Position = uViewProj * world;
}
)GLSL";

inline const char* scene_fs = R"GLSL(
precision highp float;
precision highp sampler2DShadow;

in vec3 vWorld;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vUV;
in vec4 vLightPos;

layout(location = 0) out vec4 oColor;

uniform sampler2D uAlbedo;
uniform sampler2D uNormalMap;
uniform sampler2D uORM;        // r = AO, g = roughness, b = metallic
uniform sampler2DShadow uShadow;

uniform vec3  uCamPos;
uniform vec4  uTint;           // rgb tint, a = extra emissive boost
uniform vec2  uUVScale;

// Torch
uniform vec3  uTorchPos;
uniform vec3  uTorchDir;
uniform vec3  uTorchColor;     // already multiplied by intensity
uniform float uTorchInner;     // cos of inner cone
uniform float uTorchOuter;     // cos of outer cone
uniform float uTorchRange;

// Small unshadowed lights: emergency lamps, fires, the monitor glow.
#define MAX_POINTS 6
uniform int   uPointCount;
uniform vec4  uPointPos[MAX_POINTS];    // xyz, w = radius
uniform vec4  uPointColor[MAX_POINTS];  // rgb, w = intensity

uniform vec3  uAmbient;
uniform vec3  uFogColor;
uniform float uFogDensity;
uniform float uShadowTexel;
uniform float uWetness;

const float PI = 3.14159265359;

float distribution_ggx(float ndh, float rough) {
    float a = rough * rough;
    float a2 = a * a;
    float d = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

float geometry_smith(float ndv, float ndl, float rough) {
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    float gv = ndv / (ndv * (1.0 - k) + k);
    float gl = ndl / (ndl * (1.0 - k) + k);
    return gv * gl;
}

vec3 fresnel(float cos_t, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cos_t, 0.0, 1.0), 5.0);
}

vec3 brdf(vec3 n, vec3 v, vec3 l, vec3 albedo, float rough, float metal, vec3 radiance) {
    vec3 h = normalize(v + l);
    float ndl = max(dot(n, l), 0.0);
    if (ndl <= 0.0) return vec3(0.0);
    float ndv = max(dot(n, v), 1e-4);
    float ndh = max(dot(n, h), 0.0);

    vec3 f0 = mix(vec3(0.04), albedo, metal);
    float d = distribution_ggx(ndh, rough);
    float g = geometry_smith(ndv, ndl, rough);
    vec3 f = fresnel(max(dot(h, v), 0.0), f0);

    vec3 spec = (d * g * f) / max(4.0 * ndv * ndl, 1e-4);
    vec3 kd = (vec3(1.0) - f) * (1.0 - metal);
    return (kd * albedo / PI + spec) * radiance * ndl;
}

#ifdef TRIPLANAR
// Sample by projecting on the three world axes and blending by normal. Removes
// the need for a UV unwrap on organic meshes, and hides stretching.
vec4 tri_sample(sampler2D tex, vec3 p, vec3 n, float scale) {
    vec3 b = pow(abs(n), vec3(4.0));
    b /= max(b.x + b.y + b.z, 1e-5);
    vec4 x = texture(tex, p.zy * scale);
    vec4 y = texture(tex, p.xz * scale);
    vec4 z = texture(tex, p.xy * scale);
    return x * b.x + y * b.y + z * b.z;
}

vec3 tri_normal(sampler2D tex, vec3 p, vec3 n, float scale) {
    vec3 b = pow(abs(n), vec3(4.0));
    b /= max(b.x + b.y + b.z, 1e-5);
    // Whiteout blend: perturb each axis projection, then sum in world space.
    vec3 nx = texture(tex, p.zy * scale).xyz * 2.0 - 1.0;
    vec3 ny = texture(tex, p.xz * scale).xyz * 2.0 - 1.0;
    vec3 nz = texture(tex, p.xy * scale).xyz * 2.0 - 1.0;
    nx = vec3(nx.xy + n.zy, abs(nx.z) * n.x);
    ny = vec3(ny.xy + n.xz, abs(ny.z) * n.y);
    nz = vec3(nz.xy + n.xy, abs(nz.z) * n.z);
    return normalize(nx.zyx * b.x + ny.xzy * b.y + nz.xyz * b.z);
}
#endif

float shadow_factor(vec3 n, vec3 l) {
    vec3 proj = vLightPos.xyz / max(vLightPos.w, 1e-5);
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) {
        return 1.0;
    }
    // Slope-scaled bias: grazing surfaces need far more than facing ones.
    float bias = max(0.0016 * (1.0 - dot(n, l)), 0.00035);
    float sum = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 off = vec2(float(x), float(y)) * uShadowTexel;
            sum += texture(uShadow, vec3(proj.xy + off, proj.z - bias));
        }
    }
    return sum / 9.0;
}

void main() {
    vec3 n_geom = normalize(vNormal);
    vec3 view = normalize(uCamPos - vWorld);

    vec4 albedo_s;
    vec3 n;
    vec3 orm;

#ifdef TRIPLANAR
    float s = uUVScale.x;
    albedo_s = tri_sample(uAlbedo, vWorld, n_geom, s);
    n = tri_normal(uNormalMap, vWorld, n_geom, s);
    orm = tri_sample(uORM, vWorld, n_geom, s).rgb;
#else
    vec2 uv = vUV * uUVScale;
    albedo_s = texture(uAlbedo, uv);
    vec3 tn = texture(uNormalMap, uv).xyz * 2.0 - 1.0;
    vec3 t = normalize(vTangent.xyz - n_geom * dot(n_geom, vTangent.xyz));
    vec3 b = cross(n_geom, t) * vTangent.w;
    n = normalize(mat3(t, b, n_geom) * tn);
    orm = texture(uORM, uv).rgb;
#endif

    vec3 albedo = albedo_s.rgb * uTint.rgb;
    float ao = orm.r;
    float rough = clamp(orm.g, 0.045, 1.0);
    float metal = orm.b;

    // Standing water and damp walls: smoother, darker, more reflective.
    rough = mix(rough, 0.09, uWetness);
    albedo *= mix(1.0, 0.62, uWetness);

    vec3 lo = vec3(0.0);

    // --- torch
    vec3 to_light = uTorchPos - vWorld;
    float dist = length(to_light);
    vec3 l = to_light / max(dist, 1e-4);
    float cone = dot(-l, normalize(uTorchDir));
    if (cone > uTorchOuter && dist < uTorchRange) {
        float spot = clamp((cone - uTorchOuter) / max(uTorchInner - uTorchOuter, 1e-4), 0.0, 1.0);
        spot *= spot;
        // Physical inverse-square, clamped near the bulb so standing against a
        // wall does not blow out, and windowed to zero at the stated range.
        float atten = 1.0 / max(dist * dist, 0.30);
        float window = clamp(1.0 - pow(dist / uTorchRange, 4.0), 0.0, 1.0);
        atten *= window * window;
        float shadow = shadow_factor(n, l);
        lo += brdf(n, view, l, albedo, rough, metal, uTorchColor * spot * atten * shadow);
    }

    // --- point lights
    for (int i = 0; i < MAX_POINTS; i++) {
        if (i >= uPointCount) break;
        vec3 d = uPointPos[i].xyz - vWorld;
        float pd = length(d);
        float radius = uPointPos[i].w;
        if (pd > radius) continue;
        vec3 pl = d / max(pd, 1e-4);
        float att = clamp(1.0 - pd / radius, 0.0, 1.0);
        att *= att;
        lo += brdf(n, view, pl, albedo, rough, metal,
                   uPointColor[i].rgb * uPointColor[i].w * att);
    }

    // --- ambient. A hemisphere term rather than flat, so up-facing surfaces
    // read slightly brighter and the scene keeps some shape in the dark.
    float hemi = n.y * 0.5 + 0.5;
    vec3 ambient = uAmbient * mix(0.55, 1.0, hemi) * albedo * ao;
    vec3 color = lo + ambient + albedo * uTint.a;

    // --- fog, exponential-squared with a height falloff so it pools low.
    float dcam = length(uCamPos - vWorld);
    float height_fade = exp(-max(vWorld.y - 0.2, 0.0) * 0.28);
    float f = 1.0 - exp(-pow(dcam * uFogDensity * mix(0.75, 1.0, height_fade), 2.0));
    color = mix(color, uFogColor, clamp(f, 0.0, 1.0));

    oColor = vec4(color, 1.0);
}
)GLSL";

// ----------------------------------------------------------------- shadow

inline const char* shadow_vs = R"GLSL(
precision highp float;
layout(location = 0) in vec3 aPos;
#ifdef SKINNED
layout(location = 4) in uvec4 aBone;
layout(location = 5) in vec4 aWeight;
uniform mat4 uBones[24];
#endif
uniform mat4 uModel;
uniform mat4 uViewProj;
void main() {
    vec4 p = vec4(aPos, 1.0);
#ifdef SKINNED
    mat4 skin =
        uBones[aBone.x] * aWeight.x +
        uBones[aBone.y] * aWeight.y +
        uBones[aBone.z] * aWeight.z +
        uBones[aBone.w] * aWeight.w;
    p = skin * p;
#endif
    gl_Position = uViewProj * uModel * p;
}
)GLSL";

inline const char* shadow_fs = R"GLSL(
precision highp float;
void main() {}
)GLSL";

// ------------------------------------------------------------- fullscreen

inline const char* fullscreen_vs = R"GLSL(
precision highp float;
out vec2 vUV;
void main() {
    // Single oversized triangle; gl_VertexID avoids needing any buffer at all.
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

// ------------------------------------------------------------ volumetrics
//
// Raymarched light shafts. For each pixel the ray from the eye to the scene
// surface is walked in fixed steps; at each step the torch cone is evaluated
// against the same shadow map the surfaces use, so the beam is genuinely
// occluded — doorframes cut visible wedges out of it, dust hangs in it, and the
// corridor gains the depth cue that a purely surface-lit scene has no way to
// express.
//
// Runs at half resolution with a per-pixel dither on the start offset, then gets
// blurred; without the dither, sixteen steps band like a topographic map.

inline const char* volumetric_fs = R"GLSL(
precision highp float;
precision highp sampler2DShadow;

in vec2 vUV;
layout(location = 0) out vec4 oColor;

uniform sampler2D uDepth;
uniform sampler2DShadow uShadow;

uniform mat4 uInvViewProj;
uniform mat4 uLightViewProj;
uniform vec3 uCamPos;

uniform vec3  uTorchPos;
uniform vec3  uTorchDir;
uniform vec3  uTorchColor;
uniform float uTorchInner;
uniform float uTorchOuter;
uniform float uTorchRange;

uniform float uDensity;
uniform float uTime;
uniform vec2  uResolution;

const int STEPS = 16;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Value noise, used as airborne dust rather than as a surface detail.
float noise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = i.x + i.y * 57.0 + i.z * 113.0;
    float a = fract(sin(n) * 43758.5453);
    float b = fract(sin(n + 1.0) * 43758.5453);
    float c = fract(sin(n + 57.0) * 43758.5453);
    float d = fract(sin(n + 58.0) * 43758.5453);
    float e = fract(sin(n + 113.0) * 43758.5453);
    float g = fract(sin(n + 114.0) * 43758.5453);
    float h = fract(sin(n + 170.0) * 43758.5453);
    float k = fract(sin(n + 171.0) * 43758.5453);
    return mix(mix(mix(a, b, f.x), mix(c, d, f.x), f.y),
               mix(mix(e, g, f.x), mix(h, k, f.x), f.y), f.z);
}

vec3 world_from_depth(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 w = uInvViewProj * clip;
    return w.xyz / max(w.w, 1e-6);
}

void main() {
    float depth = texture(uDepth, vUV).r;
    vec3 target = world_from_depth(vUV, depth);

    vec3 ray = target - uCamPos;
    float total = length(ray);
    // Beyond the torch's reach there is nothing left to scatter.
    total = min(total, uTorchRange);
    if (total < 0.05) { oColor = vec4(0.0); return; }
    vec3 dir = ray / max(length(ray), 1e-5);

    float step_len = total / float(STEPS);
    // Dither the entry point so the step pattern turns into noise, not stripes.
    float offset = hash12(gl_FragCoord.xy + fract(uTime) * 97.0);

    vec3 accum = vec3(0.0);
    vec3 cone_dir = normalize(uTorchDir);

    for (int i = 0; i < STEPS; i++) {
        float t = (float(i) + offset) * step_len;
        vec3 p = uCamPos + dir * t;

        vec3 to_light = uTorchPos - p;
        float d = length(to_light);
        if (d > uTorchRange) continue;
        vec3 l = to_light / max(d, 1e-4);

        float cone = dot(-l, cone_dir);
        if (cone <= uTorchOuter) continue;
        float spot = clamp((cone - uTorchOuter) / max(uTorchInner - uTorchOuter, 1e-4), 0.0, 1.0);
        spot *= spot;

        // Same shadow map as the surface pass: the beam is cut by real geometry.
        vec4 lp = uLightViewProj * vec4(p, 1.0);
        vec3 proj = lp.xyz / max(lp.w, 1e-5);
        proj = proj * 0.5 + 0.5;
        float vis = 1.0;
        if (proj.z <= 1.0 && proj.x >= 0.0 && proj.x <= 1.0 &&
            proj.y >= 0.0 && proj.y <= 1.0) {
            vis = texture(uShadow, vec3(proj.xy, proj.z - 0.0015));
        }
        if (vis <= 0.001) continue;

        // Floor the falloff a metre out: the torch sits *at* the eye, so an
        // unclamped inverse square makes the first few steps arbitrarily bright
        // and the whole frame washes to white when looking along the beam.
        float atten = 1.0 / max(d * d, 1.0);

        // Mie-ish forward scattering: the beam reads brighter looking into it.
        float phase = 0.55 + 0.45 * pow(clamp(dot(dir, -l), 0.0, 1.0), 4.0);

        // Drifting dust: the beam should never look like clean fog.
        float dust = 0.55 + 0.85 * noise3(p * 1.9 + vec3(0.0, uTime * 0.06, uTime * 0.03));
        accum += uTorchColor * spot * atten * vis * dust * phase;
    }

    // Soft rolloff rather than a hard clamp, so the beam saturates gracefully
    // instead of clipping to a white card in the middle of the screen.
    vec3 scatter = accum * uDensity * step_len;
    scatter = scatter / (1.0 + scatter * 0.55);
    oColor = vec4(scatter, 1.0);
}
)GLSL";

inline const char* bright_fs = R"GLSL(
precision mediump float;
in vec2 vUV;
layout(location = 0) out vec4 oColor;
uniform sampler2D uTex;
uniform float uThreshold;
uniform float uKnee;
void main() {
    vec3 c = texture(uTex, vUV).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    // Soft knee so bloom fades in rather than popping at the threshold.
    float soft = clamp((lum - uThreshold + uKnee) / max(2.0 * uKnee, 1e-4), 0.0, 1.0);
    float w = max(soft * soft * uKnee, max(lum - uThreshold, 0.0)) / max(lum, 1e-4);
    oColor = vec4(c * w, 1.0);
}
)GLSL";

inline const char* blur_fs = R"GLSL(
precision mediump float;
in vec2 vUV;
layout(location = 0) out vec4 oColor;
uniform sampler2D uTex;
uniform vec2 uDir;      // texel-sized step along one axis
void main() {
    // 9-tap gaussian collapsed to 5 samples using linear filtering.
    vec3 c = texture(uTex, vUV).rgb * 0.2270270270;
    c += texture(uTex, vUV + uDir * 1.3846153846).rgb * 0.3162162162;
    c += texture(uTex, vUV - uDir * 1.3846153846).rgb * 0.3162162162;
    c += texture(uTex, vUV + uDir * 3.2307692308).rgb * 0.0702702703;
    c += texture(uTex, vUV - uDir * 3.2307692308).rgb * 0.0702702703;
    oColor = vec4(c, 1.0);
}
)GLSL";

// The grade. Everything that makes it look like footage rather than a render.
inline const char* post_fs = R"GLSL(
precision highp float;
in vec2 vUV;
layout(location = 0) out vec4 oColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform sampler2D uVolumetric;
uniform float uVolumetricAmount;
uniform float uTime;
uniform float uExposure;
uniform float uBloomAmount;
uniform float uVignette;
uniform float uGrain;
uniform float uChroma;
uniform float uDesat;        // sanity loss drains colour
uniform float uRedShift;
uniform float uFlash;
uniform float uBlind;        // fade to black
uniform vec2  uResolution;
uniform float uGlitch;
uniform float uScanline;

float hash(vec2 p) {
    p = fract(p * vec2(443.8975, 397.2973));
    p += dot(p, p.yx + 19.19);
    return fract(p.x * p.y);
}

// ACES filmic, fitted. Keeps highlights from going flat white.
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec2 uv = vUV;

    // Horizontal tear bands during scares.
    if (uGlitch > 0.001) {
        float band = floor(uv.y * 28.0);
        float n = hash(vec2(band, floor(uTime * 22.0)));
        if (n > 1.0 - uGlitch * 0.45) {
            uv.x += (hash(vec2(band, uTime)) - 0.5) * 0.10 * uGlitch;
        }
    }

    // Chromatic aberration, scaled by distance from centre.
    vec2 d = uv - 0.5;
    float r2 = dot(d, d);
    float ca = (0.0018 + uChroma * 0.020) * r2 * 4.0;
    vec3 color;
    color.r = texture(uScene, uv + d * ca).r;
    color.g = texture(uScene, uv).g;
    color.b = texture(uScene, uv - d * ca).b;

    // Light shafts are added before the tonemap so they roll off with
    // everything else instead of sitting on top as a flat wash.
    color += texture(uVolumetric, uv).rgb * uVolumetricAmount;
    color += texture(uBloom, uv).rgb * uBloomAmount;
    color *= uExposure;
    color = aces(color);

    // Grade: desaturate and push toward blood as composure fails.
    float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(color, vec3(lum), uDesat);
    color = mix(color, vec3(lum * 1.5, lum * 0.28, lum * 0.22), uRedShift);

    // Vignette.
    float vig = smoothstep(0.92, 0.22, length(d) * (1.0 + uVignette));
    color *= mix(1.0, vig, 0.85);

    // Film grain, animated, stronger in shadow where a sensor would be noisiest.
    float g = hash(uv * uResolution + fract(uTime) * 913.7) - 0.5;
    color += g * uGrain * mix(1.6, 0.4, smoothstep(0.0, 0.5, lum));

    // Scanlines, very subtle — just enough to break up flat gradients.
    color *= 1.0 - uScanline * (0.5 + 0.5 * sin(uv.y * uResolution.y * 3.14159));

    color = mix(color, vec3(1.0, 0.96, 0.93), uFlash);
    color *= (1.0 - uBlind);

    oColor = vec4(color, 1.0);
}
)GLSL";

// ------------------------------------------------------------------- HUD

inline const char* ui_vs = R"GLSL(
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uScreen;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    vec2 ndc = vec2(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)GLSL";

inline const char* ui_fs = R"GLSL(
precision mediump float;
in vec2 vUV;
in vec4 vColor;
layout(location = 0) out vec4 oColor;
uniform sampler2D uTex;
uniform float uTextured;
void main() {
    vec4 t = mix(vec4(1.0), texture(uTex, vUV), uTextured);
    oColor = t * vColor;
    if (oColor.a < 0.002) discard;
}
)GLSL";

}  // namespace shaders
}  // namespace hl
