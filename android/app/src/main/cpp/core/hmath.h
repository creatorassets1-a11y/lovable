// Minimal linear algebra. Column-major matrices, right-handed, Y up.
#pragma once
#include <cmath>
#include <algorithm>

namespace hm {

constexpr float PI = 3.14159265358979323846f;
constexpr float TAU = PI * 2.0f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float radians(float d) { return d * PI / 180.0f; }
/** Frame-rate independent exponential approach. */
inline float approach(float cur, float to, float rate, float dt) {
    return cur + (to - cur) * (1.0f - std::exp(-rate * dt));
}

struct v2 {
    float x = 0, y = 0;
    v2() = default;
    v2(float x_, float y_) : x(x_), y(y_) {}
};

struct v3 {
    float x = 0, y = 0, z = 0;
    v3() = default;
    v3(float v) : x(v), y(v), z(v) {}
    v3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

inline v3 operator+(v3 a, v3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline v3 operator-(v3 a, v3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline v3 operator-(v3 a) { return {-a.x, -a.y, -a.z}; }
inline v3 operator*(v3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline v3 operator*(float s, v3 a) { return a * s; }
inline v3 operator*(v3 a, v3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline v3& operator+=(v3& a, v3 b) { a = a + b; return a; }
inline v3& operator-=(v3& a, v3 b) { a = a - b; return a; }
inline v3& operator*=(v3& a, float s) { a = a * s; return a; }
inline float dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline v3 cross(v3 a, v3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(v3 a) { return std::sqrt(dot(a, a)); }
inline float length2(v3 a) { return dot(a, a); }
inline v3 normalize(v3 a) {
    float l = length(a);
    return l > 1e-8f ? a * (1.0f / l) : v3(0, 0, 0);
}
inline v3 lerp(v3 a, v3 b, float t) { return a + (b - a) * t; }

struct v4 {
    float x = 0, y = 0, z = 0, w = 0;
    v4() = default;
    v4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    v4(v3 v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
};

/** Quaternion, xyzw. */
struct quat {
    float x = 0, y = 0, z = 0, w = 1;
    quat() = default;
    quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

inline quat operator*(quat a, quat b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

inline quat qnorm(quat q) {
    float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-8f) return {0, 0, 0, 1};
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}

inline quat slerp(quat a, quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    if (d > 0.9995f) {
        return qnorm({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                      a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    }
    float th0 = std::acos(clampf(d, -1, 1));
    float th = th0 * t;
    float s0 = std::sin(th0 - th) / std::sin(th0);
    float s1 = std::sin(th) / std::sin(th0);
    return {a.x * s0 + b.x * s1, a.y * s0 + b.y * s1,
            a.z * s0 + b.z * s1, a.w * s0 + b.w * s1};
}

inline quat quat_axis(v3 axis, float angle) {
    v3 a = normalize(axis);
    float s = std::sin(angle * 0.5f);
    return {a.x * s, a.y * s, a.z * s, std::cos(angle * 0.5f)};
}

/** Column-major 4x4: m[col*4 + row], matching GLSL's memory layout. */
struct mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float& operator()(int c, int r) { return m[c * 4 + r]; }
    float operator()(int c, int r) const { return m[c * 4 + r]; }
};

inline mat4 operator*(const mat4& a, const mat4& b) {
    mat4 o;
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + r] * b.m[c * 4 + k];
            o.m[c * 4 + r] = s;
        }
    }
    return o;
}

inline v3 transform_point(const mat4& a, v3 p) {
    return {
        a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z + a.m[12],
        a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z + a.m[13],
        a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z + a.m[14],
    };
}

inline v3 transform_dir(const mat4& a, v3 p) {
    return {
        a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z,
        a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z,
        a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z,
    };
}

inline mat4 mat4_identity() { return {}; }

inline mat4 mat4_translate(v3 t) {
    mat4 o;
    o.m[12] = t.x; o.m[13] = t.y; o.m[14] = t.z;
    return o;
}

inline mat4 mat4_scale(v3 s) {
    mat4 o;
    o.m[0] = s.x; o.m[5] = s.y; o.m[10] = s.z;
    return o;
}

inline mat4 mat4_from_quat(quat q) {
    mat4 o;
    float x = q.x, y = q.y, z = q.z, w = q.w;
    o.m[0] = 1 - 2 * (y * y + z * z); o.m[1] = 2 * (x * y + z * w);     o.m[2] = 2 * (x * z - y * w);
    o.m[4] = 2 * (x * y - z * w);     o.m[5] = 1 - 2 * (x * x + z * z); o.m[6] = 2 * (y * z + x * w);
    o.m[8] = 2 * (x * z + y * w);     o.m[9] = 2 * (y * z - x * w);     o.m[10] = 1 - 2 * (x * x + y * y);
    return o;
}

inline mat4 mat4_trs(v3 t, quat r, v3 s) {
    mat4 m = mat4_from_quat(r);
    m.m[0] *= s.x; m.m[1] *= s.x; m.m[2] *= s.x;
    m.m[4] *= s.y; m.m[5] *= s.y; m.m[6] *= s.y;
    m.m[8] *= s.z; m.m[9] *= s.z; m.m[10] *= s.z;
    m.m[12] = t.x; m.m[13] = t.y; m.m[14] = t.z;
    return m;
}

inline mat4 mat4_rotate_y(float a) {
    mat4 o;
    float c = std::cos(a), s = std::sin(a);
    o.m[0] = c; o.m[2] = -s; o.m[8] = s; o.m[10] = c;
    return o;
}

inline mat4 mat4_perspective(float fovy, float aspect, float znear, float zfar) {
    mat4 o{};
    for (int i = 0; i < 16; i++) o.m[i] = 0;
    float f = 1.0f / std::tan(fovy * 0.5f);
    o.m[0] = f / aspect;
    o.m[5] = f;
    o.m[10] = (zfar + znear) / (znear - zfar);
    o.m[11] = -1.0f;
    o.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return o;
}

inline mat4 mat4_ortho(float l, float r, float b, float t, float n, float f) {
    mat4 o;
    o.m[0] = 2 / (r - l); o.m[5] = 2 / (t - b); o.m[10] = -2 / (f - n);
    o.m[12] = -(r + l) / (r - l); o.m[13] = -(t + b) / (t - b); o.m[14] = -(f + n) / (f - n);
    return o;
}

inline mat4 mat4_look_at(v3 eye, v3 at, v3 up) {
    v3 f = normalize(at - eye);
    v3 s = normalize(cross(f, up));
    v3 u = cross(s, f);
    mat4 o;
    o.m[0] = s.x; o.m[4] = s.y; o.m[8] = s.z;
    o.m[1] = u.x; o.m[5] = u.y; o.m[9] = u.z;
    o.m[2] = -f.x; o.m[6] = -f.y; o.m[10] = -f.z;
    o.m[12] = -dot(s, eye); o.m[13] = -dot(u, eye); o.m[14] = dot(f, eye);
    return o;
}

/** General inverse. Used rarely (inverse bind matrices arrive pre-inverted). */
inline mat4 mat4_inverse(const mat4& mm) {
    const float* m = mm.m;
    float inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    mat4 o;
    if (std::fabs(det) < 1e-12f) return o;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) o.m[i] = inv[i] * det;
    return o;
}

/** Deterministic PRNG so behaviour replays identically for a given seed. */
struct Rng {
    unsigned state = 0x2545F491u;
    explicit Rng(unsigned s = 0x2545F491u) : state(s ? s : 1u) {}
    unsigned next() {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return state;
    }
    float f() { return (next() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + f() * (b - a); }
    int i(int n) { return n > 0 ? (int)(next() % (unsigned)n) : 0; }
};

}  // namespace hm
