// =============================================================================
//  scene.cpp - turns game state into draw calls.
// =============================================================================
#include "scene.h"
#include "character.h"
#include "ui.h"

#include <cstdio>
#include <cstring>

namespace {

// ---- shadow pass state -------------------------------------------------------
bool g_shadow = false;
Matrix g_shadowProj;
Surface g_surf = SurfSkin();

const Color SHADOW_COL = { 6, 4, 12, 150 };
const Vector3 LIGHT_DIR = { -0.42f, -0.78f, 0.46f };
const int TORCH_COUNT = 12;

void SetSurf(Surface s) { g_surf = s; }

void Emit(MeshId m, const Matrix &model, Color c) {
    if (g_shadow) R::Shadow(m, Mul(model, g_shadowProj), SHADOW_COL);
    else R::Mesh(m, model, c, g_surf);
}

// ---- primitive helpers -------------------------------------------------------
void Ball(const Matrix &parent, Vector3 c, float r, Color col) {
    Emit(MESH_SPHERE, Mul(MatScale(r, r, r), MatTranslate(c.x, c.y, c.z), parent), col);
}

// Squashed sphere - pecs, jaw, boots, anything that shouldn't read as a ball.
void Blob(const Matrix &parent, Vector3 c, Vector3 r, Color col) {
    Emit(MESH_SPHERE, Mul(MatScale(r.x, r.y, r.z), MatTranslate(c.x, c.y, c.z), parent), col);
}

Matrix BoneMatrix(Vector3 a, Vector3 b, float r) {
    Vector3 d = VSub(b, a);
    float len = VLen(d);
    if (len < 0.0001f) return MatScale(0, 0, 0);
    Matrix rot = MatAlignY(d * (1.0f / len));
    return Mul(MatScale(r, len, r), rot, MatTranslate(a.x, a.y, a.z));
}

void Bone(const Matrix &parent, Vector3 a, Vector3 b, float r, Color col) {
    if (VLen(VSub(b, a)) < 0.0001f) return;
    Emit(MESH_CYL, Mul(BoneMatrix(a, b, r), parent), col);
}

// Tapered segment, thick at a and thinner at b, so a forearm reads as a forearm
// rather than a pipe. Three stacked slices is plenty at this scale.
void Taper(const Matrix &parent, Vector3 a, Vector3 b, float r0, float r1, Color col) {
    Vector3 d = VSub(b, a);
    float len = VLen(d);
    if (len < 0.0001f) return;
    const int steps = 3;
    for (int i = 0; i < steps; i++) {
        float t0 = (float)i / steps, t1 = (float)(i + 1) / steps;
        Vector3 p0 = a + d * t0, p1 = a + d * t1;
        float rr = Lerpf(r0, r1, (t0 + t1) * 0.5f);
        Emit(MESH_CYL, Mul(BoneMatrix(p0, p1, rr), parent), col);
    }
}

void Box(Vector3 c, Vector3 size, float yaw, Color col) {
    Emit(MESH_CUBE, Mul(MatScale(size.x, size.y, size.z), MatRotateY(yaw),
                        MatTranslate(c.x, c.y, c.z)), col);
}

void Disc(Vector3 c, float r, float h, Color col) {
    Emit(MESH_DISC, Mul(MatScale(r, h, r), MatTranslate(c.x, c.y, c.z)), col);
}

// Deterministic per-index noise, so the crowd and the stars stay put.
float Hash(int i) {
    float x = sinf(i * 12.9898f + 78.233f) * 43758.5453f;
    return x - floorf(x);
}

Vector3 TorchPos(int i) {
    float a = i * 2 * PI / TORCH_COUNT + 0.26f;
    return { cosf(a) * 12.6f, 2.65f, sinf(a) * 12.6f };
}

// ---- two-bone IK -------------------------------------------------------------
// Returns the middle joint and clamps the end point to reach.
Vector3 SolveJoint(Vector3 a, Vector3 &b, float l1, float l2, Vector3 bend) {
    Vector3 d = VSub(b, a);
    float dist = VLen(d);
    float maxd = (l1 + l2) * 0.999f;
    if (dist < 0.02f) { d = { 0.02f, 0, 0 }; dist = 0.02f; }
    if (dist > maxd) { b = a + d * (maxd / dist); d = VSub(b, a); dist = maxd; }
    Vector3 n = d * (1 / dist);
    float x = (l1 * l1 - l2 * l2 + dist * dist) / (2 * dist);
    float h = sqrtf(fmaxf(0, l1 * l1 - x * x));
    Vector3 bp = VSub(bend, n * VDot(bend, n));
    if (VLen(bp) < 0.001f) bp = { 0, 1, 0 };
    bp = VNorm(bp);
    return a + n * x + bp * h;
}

// =============================================================================
// fighters
// =============================================================================
void DrawHead(const Matrix &parent, const Fighter &f, Vector3 head, Color skin, Color hair, Color accent) {
    Ball(parent, head, 0.125f, skin);
    Blob(parent, head + Vector3{ 0.045f, -0.055f, 0 }, { 0.085f, 0.065f, 0.085f }, skin);   // jaw
    Blob(parent, head + Vector3{ 0.108f, 0.012f, 0 }, { 0.030f, 0.026f, 0.026f }, skin);    // nose
    for (int s = -1; s <= 1; s += 2) {
        Ball(parent, head + Vector3{ 0.098f, 0.032f, 0.048f * s }, 0.020f, { 26, 22, 26, 255 });
        Blob(parent, head + Vector3{ 0.086f, 0.068f, 0.052f * s }, { 0.028f, 0.016f, 0.038f }, hair);
    }
    if (f.id == 0) {
        Blob(parent, head + Vector3{ -0.030f, 0.042f, 0 }, { 0.118f, 0.110f, 0.122f }, hair);
        Bone(parent, head + Vector3{ 0, 0.018f, 0 }, head + Vector3{ 0, 0.052f, 0 }, 0.131f, accent);
        SetSurf(SurfCloth());
        Bone(parent, head + Vector3{ -0.10f, 0.03f, 0.02f },
             head + Vector3{ -0.26f, -0.05f, 0.06f }, 0.018f, accent);       // band tail
        SetSurf(SurfSkin());
    } else {
        Blob(parent, head + Vector3{ -0.018f, 0.052f, 0 }, { 0.116f, 0.098f, 0.124f }, hair);
        Blob(parent, head + Vector3{ 0.072f, 0.086f, 0 }, { 0.050f, 0.035f, 0.070f }, hair);
    }
}

void DrawFighter(const Fighter &f) {
    if (CharacterDraw(f, g_shadow, g_shadowProj)) return;
    const Pose &p = f.pose;
    Matrix parent = FighterRoot(f);

    Color white = { 255, 255, 255, 255 };
    float hit = f.flash * 0.7f;
    Color skin = Mix(f.skin, white, hit), cloth = Mix(f.cloth, white, hit);
    Color accent = Mix(f.accent, white, hit), hair = Mix(f.hair, white, f.flash * 0.5f);
    Color trunk = Mix(cloth, { 0, 0, 0, 255 }, 0.25f);

    // rage makes the kit glow and pulse
    float rage = f.raging ? 0.35f + 0.25f * sinf(f.breath * 3.0f) : 0.0f;
    if (f.rageFlash > 0) rage = fmaxf(rage, f.rageFlash / 60.0f);

    Vector3 hip = { 0, 0, 0 };
    Vector3 tdir = { sinf(p.lean), cosf(p.lean), 0 };
    Vector3 waist = tdir * 0.16f;
    Vector3 chestLow = tdir * 0.34f;
    Vector3 chest = tdir * 0.50f;
    Vector3 neck = chest + tdir * 0.16f;
    Vector3 head = chest + tdir * 0.30f;
    float puff = 1.0f + 0.03f * sinf(f.breath);      // ribcage rise and fall

    // ---- torso ----
    SetSurf(SurfSkin());
    Taper(parent, waist, chestLow, 0.150f, 0.172f * puff, skin);
    Taper(parent, chestLow, chest, 0.172f * puff, 0.180f * puff, skin);
    Blob(parent, chest, { 0.175f, 0.165f, 0.205f }, skin);
    Blob(parent, chestLow, { 0.130f, 0.115f, 0.175f }, skin);
    for (int s = -1; s <= 1; s += 2)
        Blob(parent, chest + Vector3{ 0.070f, -0.035f, 0.088f * s }, { 0.072f, 0.070f, 0.098f }, skin);
    Bone(parent, chest, neck, 0.058f, skin);

    // ---- hips, trunks, belt ----
    SetSurf(SurfCloth());
    Blob(parent, hip + Vector3{ 0, -0.03f, 0 }, { 0.165f, 0.180f, 0.195f }, trunk);
    Blob(parent, hip + Vector3{ -0.02f, -0.14f, 0 }, { 0.150f, 0.120f, 0.185f }, trunk);
    SetSurf(SurfLeather());
    Bone(parent, hip + Vector3{ 0, 0.03f, 0 }, hip + tdir * 0.11f, 0.178f, accent);
    SetSurf({ 0.10f, 12.0f, rage * 0.8f, 0.20f });
    Blob(parent, hip + Vector3{ 0.15f, 0.06f, 0 }, { 0.050f, 0.050f, 0.038f }, accent);
    float sway = sinf(f.breath * 0.8f) * 0.05f + p.lean * 0.2f;
    SetSurf(SurfCloth());
    Taper(parent, hip + Vector3{ -0.12f, 0.02f, 0.06f },
          hip + Vector3{ -0.20f - sway, -0.34f, 0.12f }, 0.036f, 0.018f, accent);

    SetSurf(SurfSkin());
    DrawHead(parent, f, head, skin, hair, accent);

    // ---- arms ----
    for (int s = -1; s <= 1; s += 2) {
        Vector3 sh = chest + Vector3{ 0, -0.02f, 0.21f * s };
        Vector3 hand = sh + (s < 0 ? p.lHand : p.rHand);
        Vector3 elbow = SolveJoint(sh, hand, 0.30f, 0.30f, { -0.2f, -1.0f, 0.5f * s });
        SetSurf(SurfSkin());
        Blob(parent, sh, { 0.092f, 0.088f, 0.092f }, skin);
        Taper(parent, sh, elbow, 0.066f, 0.050f, skin);
        Ball(parent, elbow, 0.052f, skin);
        Taper(parent, elbow, hand, 0.055f, 0.042f, skin);
        SetSurf(SurfCloth());
        Vector3 wrist = VLerp(hand, elbow, 0.22f);
        Bone(parent, wrist, VLerp(hand, elbow, 0.05f), 0.047f, { 236, 232, 226, 255 });
        SetSurf({ 0.55f, 54.0f, rage, 0.26f });
        Blob(parent, hand, { 0.082f, 0.076f, 0.070f }, accent);
        Vector3 knuck = hand + VNorm(VSub(hand, elbow)) * 0.045f;
        Blob(parent, knuck, { 0.050f, 0.062f, 0.062f }, Mix(accent, white, 0.15f));
    }

    // ---- legs ----
    for (int s = -1; s <= 1; s += 2) {
        Vector3 hj = { 0, -0.05f, 0.11f * s };
        Vector3 foot = hj + (s < 0 ? p.lFoot : p.rFoot);
        Vector3 knee = SolveJoint(hj, foot, 0.47f, 0.47f, { 1.0f, 0.1f, 0.25f * s });
        SetSurf(SurfCloth());
        Taper(parent, hj, knee, 0.100f, 0.072f, cloth);
        Ball(parent, knee, 0.074f, cloth);
        Taper(parent, knee, foot, 0.078f, 0.052f, cloth);
        SetSurf(SurfLeather());
        Blob(parent, knee, { 0.078f, 0.070f, 0.072f }, Mix(cloth, white, 0.18f));
        Blob(parent, foot, { 0.070f, 0.062f, 0.068f }, accent);
        Blob(parent, foot + Vector3{ 0.11f, -0.020f, 0 }, { 0.085f, 0.048f, 0.060f }, accent);
    }
    SetSurf(SurfSkin());
}

// Ribbon of fading spheres following a limb through its arc.
void DrawTrail(const Fighter &f) {
    const Trail &tr = f.trail;
    if (tr.n < 2 || tr.fade <= 0.01f) return;
    for (int i = 1; i < tr.n; i++) {
        float k = (float)i / (tr.n - 1);
        float a = k * k * tr.fade;
        float r = Lerpf(0.008f, 0.032f, k);
        Color c = Fade(tr.c, a * 0.75f);
        Vector3 mid = VLerp(tr.p[i - 1], tr.p[i], 0.5f);
        R::TransFlat(MESH_SPHERE, Mul(MatScale(r, r, r), MatTranslate(mid.x, mid.y, mid.z)), c);
        float r2 = r * 0.85f;
        R::TransFlat(MESH_SPHERE, Mul(MatScale(r2, r2, r2), MatTranslate(tr.p[i].x, tr.p[i].y, tr.p[i].z)), c);
    }
}

// =============================================================================
// stage
// =============================================================================
void DrawCrowd(float t) {
    SetSurf(SurfCloth());
    Matrix idm = MatIdentity();
    for (int ring = 0; ring < 3; ring++) {
        float rad = 18.6f + ring * 2.9f;
        float base = 1.35f + ring * 1.65f;
        int count = 44 + ring * 8;
        for (int i = 0; i < count; i++) {
            float a = (i + Hash(ring * 97 + i) * 0.55f) * 2 * PI / count;
            float h = Hash(i * 31 + ring * 7);
            // a cheer ripple runs around the ring, plus a personal idle bob
            float cheer = fmaxf(0.0f, sinf(t * 1.6f - a * 3.0f)) * 0.30f;
            float bob = sinf(t * 2.4f + h * 6.28f) * 0.06f + cheer;
            Vector3 c = { cosf(a) * rad, base + bob, sinf(a) * rad };
            unsigned char v = (unsigned char)(26 + h * 26);
            Color body = { v, (unsigned char)(v * 0.92f), (unsigned char)(v * 1.25f), 255 };
            Taper(idm, c, c + Vector3{ 0, 0.62f, 0 }, 0.22f, 0.17f, body);
            Ball(idm, c + Vector3{ 0, 0.80f, 0 }, 0.17f, Mix(body, { 90, 74, 70, 255 }, 0.45f));
        }
    }
}

void DrawStage(float t) {
    const Matrix identity=MatIdentity();
    const Color stone{72,81,88,255}, dark{38,46,54,255}, bronze{136,104,65,255};
    SetSurf(SurfStone());
    Box({0,-0.8f,0},{220,1,220},0,{23,30,37,255});
    Disc({0,-0.40f,0},ARENA_R+2.0f,0.26f,dark);
    Disc({0,-0.20f,0},ARENA_R+1.2f,0.21f,stone);
    SetSurf(SurfPolish());
    Disc({0,-0.06f,0},ARENA_R+0.1f,0.065f,{76,83,87,255});
    // Bronze inlay: a restrained arena boundary and a central seal.
    for(int ring=0;ring<3;++ring)
        R::CircleY({0,0.011f,0},ARENA_R-0.09f*ring,{142,112,70,255});
    for(int ring=0;ring<2;++ring)
        R::CircleY({0,0.012f,0},2.25f+ring*0.07f,{83,84,74,255});
    for(int i=0;i<8;++i) {
        float a=i*PI/4;
        R::Line({cosf(a)*2.32f,0.012f,sinf(a)*2.32f},
                {cosf(a)*3.0f,0.012f,sinf(a)*3.0f},{100,93,74,255});
    }
    for(int i=0;i<TORCH_COUNT;++i) {
        float a=i*2*PI/TORCH_COUNT+0.26f;
        Vector3 c{cosf(a)*14.5f,0,sinf(a)*14.5f};
        SetSurf(SurfStone());
        Box(c+Vector3{0,0.25f,0},{1.6f,0.5f,1.6f},-a,stone);
        Disc(c+Vector3{0,0.5f,0},0.68f,0.25f,{88,94,98,255});
        Disc(c+Vector3{0,0.75f,0},0.48f,5.0f,stone);
        for(int band=0;band<7;++band)
            Disc(c+Vector3{0,0.82f+band*0.76f,0},0.50f,0.045f,dark);
        Disc(c+Vector3{0,5.74f,0},0.64f,0.23f,{94,99,101,255});
        Box(c+Vector3{0,6.05f,0},{1.45f,0.32f,1.45f},-a,stone);
        // Gothic arch assembled from individual stone voussoirs.
        float a2=(i+1)*2*PI/TORCH_COUNT+0.26f;
        Vector3 c2{cosf(a2)*14.5f,0,sinf(a2)*14.5f};
        Vector3 mid=(c+c2)*0.5f,axis=VNorm(c2-c);
        float radius=VLen(c2-c)*0.5f, yaw=atan2f(-axis.z,axis.x);
        for(int j=0;j<15;++j) {
            float angle=(j+0.5f)*PI/15;
            Vector3 pos=mid+axis*(cosf(angle)*radius)+Vector3{0,5.5f+sinf(angle)*radius*0.85f,0};
            float roll=atan2f(0.85f*cosf(angle),-sinf(angle));
            Emit(MESH_CUBE,Mul(MatScale(radius*PI/15*1.04f,0.63f,0.95f),
                MatRotateZ(roll),MatRotateY(yaw))*MatTranslate(pos.x,pos.y,pos.z),
                Mix(stone,{105,108,107,255},Hash(i*15+j)*0.22f));
        }
        // Low perimeter wall and recessed warm bronze panels.
        Box(mid+Vector3{0,0.66f,0},{radius*2,1.3f,0.55f},yaw,dark);
        Box(mid+Vector3{0,1.37f,0},{radius*2,0.16f,0.75f},yaw,stone);
        SetSurf(SurfMetal());
        for(int j=-2;j<=2;++j) {
            Vector3 detail=mid+axis*(j*0.72f);
            Box(detail+Vector3{0,0.7f,0},{0.04f,0.65f,0.61f},yaw,bronze);
        }
        // Braziers sit inside the colonnade, so warm pools reach the arena.
        Vector3 fire=TorchPos(i);
        SetSurf(SurfStone());
        Disc({fire.x,0,fire.z},0.46f,0.23f,dark);
        SetSurf(SurfMetal());
        Disc({fire.x,0.23f,fire.z},0.105f,1.95f,bronze);
        Emit(MESH_CONE,Mul(MatScale(0.36f,-0.30f,0.36f),MatTranslate(fire.x,2.45f,fire.z)),bronze);
        Disc({fire.x,2.40f,fire.z},0.37f,0.065f,bronze);
        float flicker=0.9f+0.10f*sinf(t*8+i*2.1f);
        SetSurf(SurfGlow(4.5f));
        Blob(identity,fire,{0.14f,0.32f*flicker,0.14f},{255,111,25,255});
        SetSurf(SurfGlow(8.0f));
        Blob(identity,fire-Vector3{0,0.06f,0},{0.07f,0.21f*flicker,0.07f},{255,211,132,255});
        for(int spark=0;spark<7;++spark) {
            float life=fmodf(t*(0.3f+Hash(spark+i*5)*0.3f)+Hash(spark*23+i),1.0f);
            Vector3 p=fire+Vector3{sinf(life*6+spark)*life*0.23f,life*1.3f,cosf(life*5+spark)*life*0.23f};
            float r=0.010f*(1-life);
            R::TransFlat(MESH_SPHERE,Mul(MatScale(r,r,r),MatTranslate(p.x,p.y,p.z)),
                         Fade(Color{255,159,64,255},(1-life)*0.7f));
        }
    }
    // Layered architecture fades into the night instead of oversized cones.
    SetSurf(SurfStone());
    for(int i=0;i<24;++i) {
        float a=i*2*PI/24;
        float h=8+Hash(i*17)*10;
        Vector3 c{cosf(a)*38,0,sinf(a)*38};
        Box(c+Vector3{0,h*0.5f,0},{5.5f,h,5.5f},-a,{33,43,53,255});
        Box(c+Vector3{0,h+0.2f,0},{6.0f,0.4f,6.0f},-a,dark);
        for(int j=-1;j<=1;++j)
            Box(c+Vector3{j*1.8f,h+0.7f,0},{0.9f,1.0f,5.7f},-a,dark);
    }
    SetSurf(SurfGlow(1.8f));
    Ball(identity,{20,22,65},2.2f,{180,206,224,255});
    SetSurf(SurfStone());
}

void DrawSky(float t) {
    int sw=0,sh=0;R::SceneSize(sw,sh);
    float w=float(sw),h=float(sh);
    R::SkyRectGradient(0,0,w,h,{7,14,23,255},{25,42,53,255});
    for(int i=0;i<65;++i) {
        float x=Hash(i*3+1)*w,y=Hash(i*3+2)*h*0.48f;
        float tw=0.7f+0.3f*sinf(t*0.4f+i);
        R::SkyDot(x,y,0.4f+Hash(i*7)*0.55f,Fade(Color{145,170,194,255},tw*0.4f));
    }
}

// =============================================================================
// camera and lighting
// =============================================================================
struct CameraInfo {
    Vector3 eye, target, up;
    float fovY;
};

float FrandS2() {
    static unsigned int r = 0x9e3779b9u;
    r ^= r << 13; r ^= r >> 17; r ^= r << 5;
    return ((float)(r & 0xFFFF) / 32768.0f) - 1.0f;
}

CameraInfo BuildCamera() {
    CameraInfo cam;
    float shake=G.reducedMotion || G.paused ? 0.0f : G.shake;
    Vector3 sh = { FrandS2() * shake, FrandS2() * shake * 0.7f, FrandS2() * shake };
    cam.target = G.camTarget + sh;
    cam.eye = cam.target + Vector3{ cosf(G.camAngle) * G.camDist,
                                    0.32f + G.camDist * 0.028f,
                                    sinf(G.camAngle) * G.camDist };
    if (G.phase==PH_TITLE) { cam.target={0.25f,1.0f,0}; cam.eye={0.10f,1.4f,-4.4f}; }
    cam.fovY = (40.0f - (G.reducedMotion ? 0.0f : G.camPunch) * 7.0f) * PI / 180.0f;
    Vector3 fw = VNorm(VSub(cam.target, cam.eye));
    Vector3 rt = VNorm(VCross(Vector3{ 0, 1, 0 }, fw));
    Vector3 up = VCross(fw, rt);
    cam.up = VNorm(up + rt * (G.reducedMotion ? 0.0f : G.camRoll));
    return cam;
}

SceneLighting BuildLighting(const CameraInfo &cam, float t) {
    SceneLighting L;
    L.viewPos = cam.eye;
    L.lightDir = LIGHT_DIR;
    L.focus = G.camTarget;

    // Only the four braziers nearest the action contribute; the rest are too far
    // to matter and the shader's loop is a fixed size.
    Vector3 mid = { (G.f[0].pos.x + G.f[1].pos.x) * 0.5f, 0, (G.f[0].pos.z + G.f[1].pos.z) * 0.5f };
    int idx[4] = { 0, 0, 0, 0 };
    float best[4] = { 1e9f, 1e9f, 1e9f, 1e9f };
    for (int i = 0; i < TORCH_COUNT; i++) {
        Vector3 tp = TorchPos(i);
        float d = (tp.x - mid.x) * (tp.x - mid.x) + (tp.z - mid.z) * (tp.z - mid.z);
        for (int s = 0; s < 4; s++) {
            if (d < best[s]) {
                for (int k = 3; k > s; k--) { best[k] = best[k - 1]; idx[k] = idx[k - 1]; }
                best[s] = d; idx[s] = i;
                break;
            }
        }
    }
    for (int s = 0; s < 4; s++) {
        L.ptPos[s] = TorchPos(idx[s]);
        float fl = 0.55f + 0.14f * sinf(t * 9 + idx[s] * 2.1f) + 0.06f * sinf(t * 23.7f + idx[s]);
        L.ptCol[s] = { 16.0f * fl, 6.5f * fl, 1.8f * fl };
    }
    Vector3 towards=VNorm(cam.eye-cam.target);
    L.ptPos[2]=G.camTarget+towards*3.0f+Vector3{-3,3,0};
    L.ptCol[2]={5.0f,6.2f,8.0f};
    L.ptPos[3]=G.camTarget-towards*2.0f+Vector3{3,2.5f,0};
    L.ptCol[3]={8.0f,3.8f,1.4f};
    return L;
}

// =============================================================================
// HUD
// =============================================================================
void DrawCombatBox(const CombatVolume &v,Color color) {
    Matrix root=Mul(MatRotateY(v.yaw),MatTranslate(v.center.x,v.center.y,v.center.z));
    Vector3 corners[8];
    for(int i=0;i<8;++i) corners[i]={(i&1)?v.half.x:-v.half.x,(i&2)?v.half.y:-v.half.y,(i&4)?v.half.z:-v.half.z};
    SetSurf({0.1f,16,0.8f,0});
    for(int i=0;i<8;++i) for(int bit : {1,2,4})
        if(!(i&bit)) Bone(root,corners[i],corners[i|bit],0.012f,color);
}

}  // namespace

// =============================================================================
// frame
// =============================================================================
void SceneDrawFrame(float t, bool postFx) {
    R::BeginFrame();
    DrawSky(t);

    CameraInfo cam = BuildCamera();
    SceneLighting L = BuildLighting(cam, t);
    Matrix view = MatLookAt(cam.eye, cam.target, cam.up);
    Matrix proj = R::Projection(cam.fovY, (float)R::Width() / (float)R::Height(), 0.1f, 400.0f);
    R::BeginScene(view, proj, L);

    // shadow projection onto y = 0.028, along the key light
    float k = -LIGHT_DIR.y;
    Matrix sp = MatIdentity();
    sp._21 = LIGHT_DIR.x / k;
    sp._22 = 0;
    sp._23 = LIGHT_DIR.z / k;
    sp._42 = 0.028f;
    g_shadowProj = sp;

    DrawStage(t);

    // soft contact patch under each fighter
    for (auto &f : G.f) {
        float r = fmaxf(0.22f, 0.52f - f.pos.y * 0.13f);
        float lie = (f.state == ST_DOWN || f.state == ST_KO) ? 0.35f : 0;
        float a = Clampf(0.42f - f.pos.y * 0.10f, 0.08f, 0.42f);
        Vector3 c = { f.pos.x - cosf(f.yaw) * lie, 0.015f, f.pos.z + sinf(f.yaw) * lie };
        float rr = r + lie;
        for(int layer=0;layer<5;++layer) {
            float radius=rr*(1.0f-layer*0.15f);
            R::Ground(MESH_DISC,Mul(MatScale(radius,0.001f,radius),
                MatTranslate(c.x,0.013f+layer*0.001f,c.z)),{3,7,10,(unsigned char)(7+layer*3)});
        }
    }

    // planar shadows: the body flattened onto the mat
    // Real directional shadows are rendered from the opaque draw queue.

    DrawFighter(G.f[0]);
    DrawFighter(G.f[1]);
    for (auto &f : G.f) DrawTrail(f);
    if(G.mode==3 && G.showHitboxes) for(const auto &f:G.f) {
        DrawCombatBox(HurtVolume(f),{70,215,255,255});
        if(f.state==ST_ATTACK && f.move!=M_WAVE && f.moveFrame>MOVES[f.move].startup &&
           f.moveFrame<=MOVES[f.move].startup+MOVES[f.move].active)
            DrawCombatBox(AttackVolume(f,f.move),{255,105,65,255});
    }

    // Projectiles use their simulated position and trajectory; glow is visual only.
    for(const auto &ball:G.projectiles) {
        Color c=ball.enhanced?Color{255,198,84,255}:ball.owner==0?Color{91,218,255,255}:Color{210,153,255,255};
        Vector3 axis=VNorm(ball.velocity);
        for(int i=5;i>=0;--i){
            float size=0.22f*(1-i*0.10f);Vector3 p=ball.pos-axis*(i*0.095f);
            R::TransFlat(MESH_SPHERE,Mul(MatScale(size,size*0.82f,size),MatTranslate(p.x,p.y,p.z)),Fade(c,0.26f+i*0.055f));
        }
        Vector3 p=ball.pos;
        R::TransFlat(MESH_SPHERE,Mul(MatScale(0.13f,0.13f,0.13f),MatTranslate(p.x,p.y,p.z)),{240,252,255,245});
        R::CircleFacing(p,0.25f,axis,c);
        if(G.mode==3 && G.showHitboxes)DrawCombatBox({p,{0.28f,0.22f,0.28f},0},{255,165,75,255});
    }
    for(const auto &f:G.f){
        bool powered=f.heatFrames>0 || f.enhanced && f.state==ST_ATTACK;
        bool drive=f.state==ST_RUSH || f.state==ST_PARRY || f.state==ST_ATTACK && f.move==M_IMPACT;
        if(powered || drive){
            Color c=drive?Color{80,235,202,160}:Color{255,167,72,150};
            float pulse=G.reducedMotion?0:sinf(t*8)*0.04f;
            R::CircleY(f.pos+Vector3{0,0.05f,0},0.48f+pulse,c);
            R::CircleY(f.pos+Vector3{0,0.07f,0},0.56f+pulse,Fade(c,0.45f));
            if(f.state==ST_PARRY)R::CircleFacing(f.pos+Vector3{0,1,0},0.72f,{cosf(f.yaw),0,-sinf(f.yaw)},c);
        }
    }

    for (auto &p : G.parts) {
        float a = Clampf(p.life, 0, 1);
        float s = p.size;
        Color c = Fade(p.c, p.kind == 1 ? a * 0.45f : a);
        MeshId m = p.kind == 1 ? MESH_SPHERE : MESH_CUBE;
        R::TransFlat(m, Mul(MatScale(s, s, s), MatTranslate(p.p.x, p.p.y, p.p.z)), c);
    }
    for (auto &fl : G.flashes) {
        float r = 0.04f + fl.t * 0.16f;
        R::TransFlat(MESH_SPHERE, Mul(MatScale(r, r, r), MatTranslate(fl.p.x, fl.p.y, fl.p.z)),
                     Fade(fl.c, (1 - fl.t) * 0.55f));
        float r2 = 0.025f + fl.t * 0.06f;
        R::TransFlat(MESH_SPHERE, Mul(MatScale(r2, r2, r2), MatTranslate(fl.p.x, fl.p.y, fl.p.z)),
                     Fade(Color{ 255, 255, 255, 255 }, (1 - fl.t) * 0.60f));
        // expanding impact rings
        R::CircleFacing(fl.p, 0.10f + fl.t * 1.05f, { 0, 1, 0 }, Fade(fl.c, (1 - fl.t) * 0.45f));
        R::CircleFacing(fl.p, 0.10f + fl.t * 1.05f, VNorm(VSub(cam.eye, fl.p)),
                        Fade(fl.c, (1 - fl.t) * 0.35f));
    }

    R::EndScene();

    PostParams pp;
    pp.time = t;
    pp.flashCol = G.flashCol;
    pp.flashAmt = G.reducedMotion ? 0.0f : G.screenFlash;
    if (!postFx) { pp.bloom = 0; pp.vignette = 0; pp.aberration = 0; pp.grain = 0; }
    R::Post(pp);

    DrawGameUI(t);
    R::EndFrame();
}
