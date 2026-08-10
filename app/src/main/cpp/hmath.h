// hmath.h - minimal 3D math for HOLLOW SIGNAL
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace hm {

constexpr float PI = 3.14159265358979323846f;
constexpr float TAU = 6.28318530717958647692f;
constexpr float DEG2RAD = PI / 180.0f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
inline float signf(float v) { return v < 0.0f ? -1.0f : 1.0f; }
// Move `a` toward `b` by at most `maxDelta`, wrapping correctly across +/-PI.
inline float angleTowards(float a, float b, float maxDelta) {
    float d = b - a;
    while (d > PI) d -= TAU;
    while (d < -PI) d += TAU;
    if (d > maxDelta) d = maxDelta;
    if (d < -maxDelta) d = -maxDelta;
    return a + d;
}

struct vec2 {
    float x = 0, y = 0;
    vec2() {}
    vec2(float x_, float y_) : x(x_), y(y_) {}
    vec2 operator+(const vec2& o) const { return {x + o.x, y + o.y}; }
    vec2 operator-(const vec2& o) const { return {x - o.x, y - o.y}; }
    vec2 operator*(float s) const { return {x * s, y * s}; }
    vec2& operator+=(const vec2& o) { x += o.x; y += o.y; return *this; }
};
inline float length(const vec2& v) { return std::sqrt(v.x * v.x + v.y * v.y); }

struct vec3 {
    float x = 0, y = 0, z = 0;
    vec3() {}
    vec3(float v) : x(v), y(v), z(v) {}
    vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    vec3 operator+(const vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    vec3 operator-(const vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    vec3 operator-() const { return {-x, -y, -z}; }
    vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    vec3 operator*(const vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    // Componentwise divide - used for ellipsoid normals, where the surface
    // normal is the direction scaled by the inverse of the radii.
    vec3 operator/(const vec3& o) const { return {x / o.x, y / o.y, z / o.z}; }
    vec3& operator+=(const vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    vec3& operator-=(const vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};

inline float dot(const vec3& a, const vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(const vec3& a, const vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const vec3& v) { return std::sqrt(dot(v, v)); }
inline float lengthSq(const vec3& v) { return dot(v, v); }
inline vec3 normalize(const vec3& v) {
    float l = length(v);
    return l > 1e-8f ? v * (1.0f / l) : vec3(0, 0, 0);
}
inline vec3 lerp(const vec3& a, const vec3& b, float t) { return a + (b - a) * t; }
inline vec3 minv(const vec3& a, const vec3& b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
inline vec3 maxv(const vec3& a, const vec3& b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

struct vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    vec4() {}
    vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    vec4(const vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
};

// Column-major 4x4, matching OpenGL's expected memory layout.
// m[col*4 + row]
struct mat4 {
    float m[16];

    mat4() { identity(); }

    void identity() {
        for (int i = 0; i < 16; i++) m[i] = 0.0f;
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    static mat4 translation(const vec3& t) {
        mat4 r;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }

    static mat4 scale(const vec3& s) {
        mat4 r;
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
        return r;
    }

    static mat4 rotationX(float a) {
        mat4 r; float c = std::cos(a), s = std::sin(a);
        r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
        return r;
    }

    static mat4 rotationY(float a) {
        mat4 r; float c = std::cos(a), s = std::sin(a);
        r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
        return r;
    }

    static mat4 rotationZ(float a) {
        mat4 r; float c = std::cos(a), s = std::sin(a);
        r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
        return r;
    }

    static mat4 perspective(float fovyRad, float aspect, float zn, float zf) {
        mat4 r;
        for (int i = 0; i < 16; i++) r.m[i] = 0.0f;
        float f = 1.0f / std::tan(fovyRad * 0.5f);
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zf + zn) / (zn - zf);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * zf * zn) / (zn - zf);
        return r;
    }

    static mat4 ortho(float l, float r_, float b, float t, float zn, float zf) {
        mat4 r;
        r.m[0] = 2.0f / (r_ - l);
        r.m[5] = 2.0f / (t - b);
        r.m[10] = -2.0f / (zf - zn);
        r.m[12] = -(r_ + l) / (r_ - l);
        r.m[13] = -(t + b) / (t - b);
        r.m[14] = -(zf + zn) / (zf - zn);
        return r;
    }

    static mat4 lookAt(const vec3& eye, const vec3& center, const vec3& up) {
        vec3 f = normalize(center - eye);
        vec3 s = normalize(cross(f, up));
        vec3 u = cross(s, f);
        mat4 r;
        r.m[0] = s.x; r.m[4] = s.y; r.m[8]  = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9]  = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -dot(s, eye);
        r.m[13] = -dot(u, eye);
        r.m[14] = dot(f, eye);
        return r;
    }

    mat4 operator*(const mat4& o) const {
        mat4 r;
        for (int c = 0; c < 4; c++) {
            for (int row = 0; row < 4; row++) {
                float sum = 0.0f;
                for (int k = 0; k < 4; k++) sum += m[k * 4 + row] * o.m[c * 4 + k];
                r.m[c * 4 + row] = sum;
            }
        }
        return r;
    }

    vec3 transformPoint(const vec3& p) const {
        return {
            m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12],
            m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]
        };
    }

    vec3 transformDir(const vec3& p) const {
        return {
            m[0] * p.x + m[4] * p.y + m[8]  * p.z,
            m[1] * p.x + m[5] * p.y + m[9]  * p.z,
            m[2] * p.x + m[6] * p.y + m[10] * p.z
        };
    }
};

// Deterministic PRNG so a given chapter seed always builds the same level.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 0x1234567u) : s(seed ? seed : 0x1234567u) {}
    uint32_t next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    }
    float f01() { return (next() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + f01() * (b - a); }
    int rangei(int a, int b) { return b <= a ? a : a + (int)(next() % (uint32_t)(b - a)); }
};

} // namespace hm
