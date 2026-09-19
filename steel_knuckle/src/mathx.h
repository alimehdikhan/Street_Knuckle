// =============================================================================
//  mathx.h - the small slice of maths the game needs.
//
//  Built on Diligent's BasicMath, which uses row-vector convention (v * M), so
//  Mul(a, b) means "apply a, then b" - the same reading order the original
//  raylib code used.
// =============================================================================
#pragma once

#include <cmath>
#include <algorithm>

#include "BasicMath.hpp"

using Diligent::float2;
using Diligent::float3;
using Diligent::float4;
using Diligent::float4x4;

using Vector3 = float3;
using Matrix = float4x4;

#ifndef PI
#define PI 3.14159265358979323846f
#endif

struct Color {
    unsigned char r = 0, g = 0, b = 0, a = 255;
};

inline Color Rgb(int r, int g, int b, int a = 255) {
    return { (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a };
}
inline float4 ToF4(Color c) {
    return { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f };
}

// ---- scalar helpers ----
inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Shortest signed angle from a to b, in (-PI, PI].
inline float AngleDiff(float a, float b) {
    float d = fmodf(b - a + PI, 2 * PI);
    if (d < 0) d += 2 * PI;
    return d - PI;
}

// ---- vector helpers ----
inline Vector3 VAdd(Vector3 a, Vector3 b) { return a + b; }
inline Vector3 VSub(Vector3 a, Vector3 b) { return a - b; }
inline Vector3 VMul(Vector3 a, float s) { return a * s; }
inline float VDot(Vector3 a, Vector3 b) { return Diligent::dot(a, b); }
inline Vector3 VCross(Vector3 a, Vector3 b) { return Diligent::cross(a, b); }
inline float VLen(Vector3 a) { return Diligent::length(a); }
inline Vector3 VNorm(Vector3 a) {
    float l = VLen(a);
    return l > 1e-6f ? a / l : Vector3{ 0, 0, 0 };
}
inline Vector3 VLerp(Vector3 a, Vector3 b, float t) { return a + (b - a) * t; }

// ---- matrix helpers, named to read like the original ----
inline Matrix MatIdentity() { return float4x4::Identity(); }
inline Matrix MatTranslate(float x, float y, float z) { return float4x4::Translation(x, y, z); }
inline Matrix MatScale(float x, float y, float z) { return float4x4::Scale(x, y, z); }
inline Matrix MatRotateX(float r) { return float4x4::RotationX(r); }
inline Matrix MatRotateY(float r) { return float4x4::RotationY(r); }
inline Matrix MatRotateZ(float r) { return float4x4::RotationZ(r); }

// Apply a first, then b.
inline Matrix Mul(const Matrix &a, const Matrix &b) { return a * b; }
inline Matrix Mul(const Matrix &a, const Matrix &b, const Matrix &c) { return a * b * c; }

// Rotation of `angle` radians about an arbitrary axis.
inline Matrix MatRotateAxis(Vector3 axis, float angle) {
    return float4x4::RotationArbitrary(axis, angle);
}

inline Vector3 XformPoint(Vector3 p, const Matrix &m) {
    float4 r = float4(p.x, p.y, p.z, 1.0f) * m;
    return { r.x, r.y, r.z };
}

// Rotation that takes +Y onto the unit vector n. Used to stand a unit cylinder
// up along an arbitrary bone direction.
inline Matrix MatAlignY(Vector3 n) {
    if (n.y > 0.9999f) return MatIdentity();
    if (n.y < -0.9999f) return MatRotateX(PI);
    Vector3 axis = VCross(Vector3{ 0, 1, 0 }, n);
    return MatRotateAxis(VNorm(axis), acosf(Clampf(n.y, -1.0f, 1.0f)));
}

// Left-handed look-at for row vectors, matching Diligent's default handedness
// and its projection matrices.
inline Matrix MatLookAt(Vector3 eye, Vector3 target, Vector3 up) {
    Vector3 z = VNorm(VSub(target, eye));
    Vector3 x = VNorm(VCross(up, z));
    Vector3 y = VCross(z, x);
    Matrix m = MatIdentity();
    m._11 = x.x; m._12 = y.x; m._13 = z.x; m._14 = 0;
    m._21 = x.y; m._22 = y.y; m._23 = z.y; m._24 = 0;
    m._31 = x.z; m._32 = y.z; m._33 = z.z; m._34 = 0;
    m._41 = -VDot(x, eye); m._42 = -VDot(y, eye); m._43 = -VDot(z, eye); m._44 = 1;
    return m;
}

inline Color Mix(Color a, Color b, float t) {
    t = Clampf(t, 0, 1);
    return { (unsigned char)(a.r + (b.r - a.r) * t),
             (unsigned char)(a.g + (b.g - a.g) * t),
             (unsigned char)(a.b + (b.b - a.b) * t),
             a.a };
}
inline Color Fade(Color c, float alpha) {
    c.a = (unsigned char)(Clampf(alpha, 0, 1) * 255.0f);
    return c;
}
