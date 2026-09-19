// =============================================================================
//  STEEL KNUCKLE  -  a Tekken-3-style 3D fighting game in C++ / raylib
//
//  Build (Linux):   g++ main.cpp -o steel_knuckle -std=c++17 -O2 -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
//  Build (Windows, MinGW):  g++ main.cpp -o steel_knuckle.exe -std=c++17 -O2 -lraylib -lopengl32 -lgdi32 -lwinmm
//  Build (macOS):   clang++ main.cpp -o steel_knuckle -std=c++17 -O2 -lraylib -framework OpenGL -framework Cocoa -framework IOKit
//  Or use the CMakeLists.txt next to this file (downloads raylib for you).
//
//  Needs raylib 4.5 or newer.
//
//  What is in here:
//    - 3D arena, fighters built from lit capsules with 2-bone IK and blended poses
//    - 4 limb buttons (LP, RP, LK, RK), command moves, a 1-2 string
//    - High / Mid / Low hit levels, standing and crouching guard
//    - Sidestep into background / foreground, dashes, hop
//    - Launcher + juggle system with damage scaling and heavier gravity per hit
//    - Throw (duckable), knockdowns, ground hits, counter hits
//    - Hitstop, camera shake, KO slow motion, side-on tracking camera
//    - CPU opponent (3 difficulties), 2-player versus, CPU vs CPU demo
//    - Procedurally generated sound effects (no asset files needed)
// =============================================================================

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------
static const int   SCREEN_W      = 1280;
static const int   SCREEN_H      = 720;
static const float DT            = 1.0f / 60.0f;
static const float ARENA_R       = 11.0f;
static const int   MAX_HP        = 130;
static const int   ROUND_FRAMES  = 60 * 60;
static const int   ROUNDS_TO_WIN = 2;
static const float BODY_DIST     = 0.72f;

// ----------------------------------------------------------------------------
// Move data
// ----------------------------------------------------------------------------
enum Level  { LV_HIGH, LV_MID, LV_LOW, LV_THROW };
enum Effect { FX_NORMAL, FX_KNOCKDOWN, FX_LAUNCH, FX_SWEEP };
enum Limb   { L_HAND, R_HAND, L_FOOT, R_FOOT, BOTH_HANDS };
enum MoveId { M_NONE = 0, M_LP, M_RP, M_LK, M_RK, M_DLP, M_DRP, M_DLK, M_DRK, M_FRP, M_THROW, M_COUNT };

struct MoveDef {
    const char *name, *input;
    int   startup, active, recovery;
    Level level;
    int   damage;
    float range;
    int   hitstun, blockstun;
    float pushHit, pushBlock;
    Effect effect;
    float lunge;
    bool  crouching;              // attacker counts as crouching (goes under highs)
    Limb  limb;
    Vector3 windup, strike;       // limb target relative to shoulder / hip joint
    float leanWind, leanStrike;
    float hipYWind, hipY;
};

static const MoveDef MOVES[M_COUNT] = {
    { "", "", 0,0,0, LV_HIGH, 0, 0, 0,0, 0,0, FX_NORMAL, 0, false, L_HAND, {0,0,0}, {0,0,0}, 0,0, 0.92f,0.92f },
    { "Jab",             "LP",     10,3,13, LV_HIGH,  6, 1.25f, 21,17, 0.050f,0.040f, FX_NORMAL,    0.12f, false, L_HAND,     { 0.10f,-0.05f, 0.05f}, {0.60f, 0.04f, 0.12f},  0.05f, 0.22f, 0.92f,0.92f },
    { "Straight",        "RP",     12,3,17, LV_HIGH,  9, 1.35f, 23,17, 0.070f,0.050f, FX_NORMAL,    0.15f, false, R_HAND,     {-0.05f,-0.08f,-0.02f}, {0.62f, 0.03f,-0.14f},  0.00f, 0.32f, 0.92f,0.92f },
    { "Mid Kick",        "LK",     14,3,21, LV_MID,  13, 1.60f, 26,16, 0.080f,0.060f, FX_NORMAL,    0.15f, false, L_FOOT,     { 0.25f,-0.40f, 0.00f}, {0.88f,-0.12f, 0.05f}, -0.05f,-0.30f, 0.94f,0.94f },
    { "Roundhouse",      "RK",     16,4,25, LV_HIGH, 18, 1.65f,  0,18, 0.000f,0.070f, FX_KNOCKDOWN, 0.20f, false, R_FOOT,     { 0.10f,-0.35f, 0.20f}, {0.76f, 0.52f,-0.05f}, -0.10f,-0.45f, 0.95f,0.96f },
    { "Crouch Jab",      "d+LP",   10,3,14, LV_MID,   5, 1.20f, 20,14, 0.050f,0.040f, FX_NORMAL,    0.08f, true,  L_HAND,     { 0.10f,-0.10f, 0.05f}, {0.60f,-0.05f, 0.12f},  0.30f, 0.45f, 0.58f,0.58f },
    { "Rising Uppercut", "d+RP",   15,3,26, LV_MID,  14, 1.30f,  0,18, 0.000f,0.050f, FX_LAUNCH,    0.20f, false, R_HAND,     { 0.15f,-0.45f,-0.05f}, {0.42f, 0.42f,-0.12f},  0.50f,-0.12f, 0.65f,0.98f },
    { "Low Kick",        "d+LK",   16,3,23, LV_LOW,   8, 1.55f, 23,12, 0.040f,0.030f, FX_NORMAL,    0.10f, true,  L_FOOT,     { 0.20f,-0.35f, 0.00f}, {0.88f,-0.48f, 0.05f},  0.20f, 0.10f, 0.58f,0.58f },
    { "Sweep",           "d+RK",   22,4,33, LV_LOW,  15, 1.75f,  0,12, 0.000f,0.030f, FX_SWEEP,     0.20f, true,  R_FOOT,     {-0.10f,-0.40f, 0.30f}, {0.92f,-0.42f,-0.05f},  0.30f, 0.15f, 0.52f,0.50f },
    { "Power Straight",  "f+RP",   18,3,27, LV_MID,  20, 1.60f,  0,22, 0.000f,0.100f, FX_KNOCKDOWN, 0.55f, false, R_HAND,     {-0.15f,-0.10f,-0.05f}, {0.64f,-0.10f,-0.14f}, -0.10f, 0.50f, 0.90f,0.85f },
    { "Shoulder Throw",  "LP+LK",  12,2,27, LV_THROW,28, 1.15f,  0, 0, 0.000f,0.000f, FX_NORMAL,    0.20f, false, BOTH_HANDS, { 0.15f,-0.10f, 0.00f}, {0.50f, 0.00f, 0.00f},  0.10f, 0.30f, 0.92f,0.90f },
};

// ----------------------------------------------------------------------------
// Core types
// ----------------------------------------------------------------------------
struct Input {
    bool fwd = false, back = false, up = false, down = false;
    bool dashF = false, dashB = false;
    bool ssBg = false, ssFg = false;                 // sidestep into background / foreground
    bool lp = false, rp = false, lk = false, rk = false;      // held
    bool lpP = false, rpP = false, lkP = false, rkP = false;  // pressed this frame
};

struct Pose {
    float hipY = 0.92f, lean = 0.12f, tilt = 0.0f;   // tilt: whole body pitches backward (radians)
    Vector3 lHand = {0.34f,-0.02f, 0.08f}, rHand = {0.22f,-0.12f,-0.06f};
    Vector3 lFoot = {0.24f,-0.81f,-0.04f}, rFoot = {-0.24f,-0.81f, 0.06f};
};

enum State {
    ST_IDLE, ST_CROUCH, ST_DASH, ST_SIDESTEP, ST_JUMP, ST_ATTACK, ST_HIT, ST_BLOCK,
    ST_LAUNCH, ST_DOWN, ST_GETUP, ST_THROWING, ST_THROWN, ST_KO, ST_WIN
};

#define TRAIL_LEN 14

// Swept arc left behind a striking limb. Points are world space, newest last.
struct Trail {
    Vector3 p[TRAIL_LEN]{};
    int n = 0;
    float fade = 0;
    Color c{};
};

struct Fighter {
    int id = 0;
    const char *name = "";
    Color skin{}, cloth{}, accent{}, hair{};
    Vector3 pos{}, kb{};
    float vy = 0, yaw = 0;
    State state = ST_IDLE;
    int timer = 0;
    bool crouched = false;
    int move = 0, moveFrame = 0, serial = 0;
    bool moveHit = false;
    int buf = 0, bufT = 0;
    int hp = MAX_HP;
    float hpLag = MAX_HP;
    int juggle = 0;
    int comboCount = 0, comboDmg = 0;            // hits taken in the current combo
    int showCount = 0, showDmg = 0, showT = 0;   // combo this fighter is dealing (HUD)
    int ssDir = 0, dashDir = 0;
    float jumpVx = 0;
    bool ko = false, walking = false;
    int walkSign = 1;
    int wins = 0;
    float flash = 0, walkPhase = 0;
    const char *msg = ""; int msgT = 0;
    Input in;
    Pose pose;

    // --- added state ---
    Trail trail;
    int meter = 0;                  // rage meter, fills from damage dealt and taken
    bool raging = false;            // meter full: rage art available, body glows
    int rageFlash = 0;
    int parryT = 0;                 // frames left of a successful parry's freeze
    int escapeT = 0;                // window to break a throw with LP
    int guardHistory = 0;           // +1 blocked high, -1 blocked low; AI reads this
    int whiffT = 0;                 // counts recovery frames after a whiffed attack
    float breath = 0;               // idle breathing phase, desynced per fighter
    float sweat = 0;                // rises as HP drops, drives drip particles
    int stagger = 0;                // wall/edge stagger frames
};

struct AIState {
    int think = 30, seen = -1, guardT = 0;
    bool guardLow = false, guardBack = true;
    int qBtn = 0, qDelay = 0;
    int walkT = 0, walkDir = 0;
};

struct Particle { Vector3 p, v; float life, size; Color c; };
struct Flash    { Vector3 p; float t; Color c; };

enum Phase { PH_TITLE, PH_INTRO, PH_FIGHT, PH_ROUND_END, PH_MATCH_END };
enum SoundId { SND_HIT, SND_HEAVY, SND_BLOCK, SND_WHOOSH, SND_KO, SND_BELL, SND_THUD, SND_COUNT };

struct Game {
    Phase phase = PH_TITLE;
    int mode = 0;            // 0 = 1P vs CPU, 1 = 2P versus, 2 = CPU vs CPU
    int difficulty = 1;
    int round = 1, timer = ROUND_FRAMES, phaseT = 0;
    int hitstop = 0, slowmo = 0;
    float shake = 0;
    bool paused = false;
    char banner[48] = ""; int bannerT = 0;
    float camAngle = PI * 0.5f, camDist = 7.0f;
    Vector3 camTarget = {0, 1, 0};
    Vector3 bgDir = {0, 0, -1};
    std::vector<Particle> parts;
    std::vector<Flash> flashes;
    Sound snd[SND_COUNT]{};
    bool audio = false;
    long frame = 0;
    int winner = -1;
    Mesh sphere{}, cyl{}, cube{}, cone{}, disc{};
    Material mat{};
    Shader lit{}, bright{}, blur{}, composite{};
    RenderTexture2D scene{}, bloomA{}, bloomB{};
    int locLight = -1, locView = -1, locKey = -1, locSky = -1, locGnd = -1, locRim = -1;
    int locFogCol = -1, locPtPos = -1, locPtCol = -1;
    int locSurf = -1, locGloss = -1, locEmis = -1, locRimAmt = -1, locFogAmt = -1;
    int locBlurDir = -1;
    int locBloomTex = -1, locBloomAmt = -1, locVig = -1, locAberr = -1, locGrain = -1;
    int locTime = -1, locFlashAmt = -1, locFlashCol = -1;
    float screenFlash = 0;                       // full-screen impact flash, drives composite
    Vector3 flashCol = { 1.0f, 0.85f, 0.6f };
    bool postFx = true;
    Shader flat{};                               // raylib's unlit default, used for shadows
    bool shadowPass = false;
    Matrix shadowProj{};                         // world -> flattened onto the arena floor
    float camRoll = 0, camPunch = 0;             // impact roll and FOV kick
    float hitLag = 0;                            // eases the camera in during hitstop
    Fighter f[2];
    AIState ai[2];
    int lastFwdTap[2] = {-100, -100}, lastBackTap[2] = {-100, -100};
    bool wasFwd[2] = {false, false}, wasBack[2] = {false, false};
};
static Game G;

// ----------------------------------------------------------------------------
// Small helpers
// ----------------------------------------------------------------------------
static float Frand() { return (float)GetRandomValue(0, 10000) / 10000.0f; }
static float FrandS() { return Frand() * 2.0f - 1.0f; }
static float AngleDiff(float a, float b) { float d = fmodf(b - a + PI, 2 * PI); if (d < 0) d += 2 * PI; return d - PI; }
static Vector3 Fwd(const Fighter &f)  { return { cosf(f.yaw), 0, -sinf(f.yaw) }; }
static Vector3 Side(const Fighter &f) { return { sinf(f.yaw), 0,  cosf(f.yaw) }; }
static Vector3 VAdd(Vector3 a, Vector3 b) { return Vector3Add(a, b); }
static Vector3 VMul(Vector3 a, float s)   { return Vector3Scale(a, s); }
static Color Mix(Color a, Color b, float t) {
    t = Clamp(t, 0, 1);
    return { (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
             (unsigned char)(a.b + (b.b - a.b) * t), a.a };
}
static void Play(SoundId s) { if (G.audio) PlaySound(G.snd[s]); }
static void SetBanner(const char *t, int frames) { snprintf(G.banner, sizeof(G.banner), "%s", t); G.bannerT = frames; }
static void SetMsg(Fighter &f, const char *m) { f.msg = m; f.msgT = 55; }

// Defined with the renderer, but the simulation needs them to sample limb arcs.
static Vector3 LimbWorld(const Fighter &f, Limb limb);
static void UpdateTrail(Fighter &f);

static void SpawnSparks(Vector3 p, Color c, int n, float speed) {
    for (int i = 0; i < n; i++) {
        Particle q;
        q.p = p;
        q.v = { FrandS() * speed, Frand() * speed * 0.9f + 0.01f, FrandS() * speed };
        q.life = 0.6f + Frand() * 0.4f;
        q.size = 0.03f + Frand() * 0.04f;
        q.c = c;
        G.parts.push_back(q);
    }
    G.flashes.push_back({ p, 0, c });
}

// ----------------------------------------------------------------------------
// Procedural sound effects
// ----------------------------------------------------------------------------
static Sound MakeSound(SoundId id) {
    const int sr = 22050;
    float dur = 0.1f, freq = 140, noiseAmt = 0.7f, decay = 30, lp = 0.35f, vol = 0.8f, sweep = 0;
    switch (id) {
        case SND_HIT:    dur = 0.12f; freq = 150; noiseAmt = 0.75f; decay = 32; lp = 0.35f; vol = 0.80f; break;
        case SND_HEAVY:  dur = 0.22f; freq = 85;  noiseAmt = 0.70f; decay = 17; lp = 0.25f; vol = 0.95f; break;
        case SND_BLOCK:  dur = 0.07f; freq = 820; noiseAmt = 0.50f; decay = 55; lp = 0.90f; vol = 0.40f; break;
        case SND_WHOOSH: dur = 0.14f; freq = 0;   noiseAmt = 1.00f; decay = 0;  lp = 0.12f; vol = 0.30f; break;
        case SND_KO:     dur = 0.80f; freq = 60;  noiseAmt = 0.50f; decay = 5;  lp = 0.15f; vol = 1.00f; sweep = -25; break;
        case SND_BELL:   dur = 0.60f; freq = 880; noiseAmt = 0.00f; decay = 6;  lp = 1.00f; vol = 0.45f; break;
        case SND_THUD:   dur = 0.16f; freq = 70;  noiseAmt = 0.40f; decay = 24; lp = 0.12f; vol = 0.70f; break;
        default: break;
    }
    int n = (int)(dur * sr);
    short *data = (short *)MemAlloc(n * sizeof(short));
    float low = 0, phase = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr;
        float env = (id == SND_WHOOSH) ? sinf(PI * t / dur) : expf(-decay * t);
        float noise = FrandS();
        low += (noise - low) * lp;
        phase += 2 * PI * (freq + sweep * t) / sr;
        float tone = sinf(phase);
        if (id == SND_BELL) tone = 0.6f * tone + 0.4f * sinf(phase * 2.01f);
        float s = (low * noiseAmt + tone * (1 - noiseAmt)) * env * vol;
        data[i] = (short)(Clamp(s, -1, 1) * 32000);
    }
    Wave w{};
    w.frameCount = (unsigned int)n; w.sampleRate = sr; w.sampleSize = 16; w.channels = 1; w.data = data;
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

// ----------------------------------------------------------------------------
// Shaders
//
//  lit       - hemisphere ambient + key/fill/rim + Blinn-Phong spec + torch
//              point lights + height fog. Per-draw surface params via uniforms.
//  bright    - bloom threshold prepass
//  blur      - separable 9-tap gaussian
//  composite - scene + bloom, ACES tonemap, grade, vignette, aberration, grain
// ----------------------------------------------------------------------------
#define MAX_PT_LIGHTS 4

static const char *VS_SRC =
    "#version 330\n"
    "in vec3 vertexPosition; in vec3 vertexNormal;\n"
    "uniform mat4 mvp; uniform mat4 matModel; uniform mat4 matNormal;\n"
    "out vec3 fragPos; out vec3 fragNormal;\n"
    "void main(){ fragPos = vec3(matModel*vec4(vertexPosition,1.0));\n"
    "  fragNormal = normalize(vec3(matNormal*vec4(vertexNormal,0.0)));\n"
    "  gl_Position = mvp*vec4(vertexPosition,1.0); }\n";

static const char *FS_SRC =
    "#version 330\n"
    "in vec3 fragPos; in vec3 fragNormal;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 lightDir;      // direction the key light travels\n"
    "uniform vec3 viewPos;\n"
    "uniform vec3 keyCol;        // key light colour\n"
    "uniform vec3 skyCol;        // hemisphere ambient, from above\n"
    "uniform vec3 gndCol;        // hemisphere ambient, bounced from the floor\n"
    "uniform vec3 rimCol;\n"
    "uniform vec3 fogCol;\n"
    "uniform vec3 ptPos[4];\n"
    "uniform vec3 ptCol[4];\n"
    "uniform float surf;         // x: specular strength\n"
    "uniform float gloss;        // specular exponent\n"
    "uniform float emis;         // self-illumination, drives the bloom pass\n"
    "uniform float rimAmt;\n"
    "uniform float fogAmt;\n"
    "out vec4 finalColor;\n"
    "void main(){\n"
    "  vec3 n = normalize(fragNormal);\n"
    "  vec3 v = normalize(viewPos - fragPos);\n"
    "  vec3 l = -normalize(lightDir);\n"
    "  vec3 base = colDiffuse.rgb;\n"
    // wrapped diffuse keeps the shadow side readable instead of crushing to black
    "  float ndl = dot(n, l);\n"
    "  float key = max((ndl + 0.35) / 1.35, 0.0);\n"
    "  float fill = max(dot(n, normalize(vec3(-l.x, 0.25, -l.z))), 0.0) * 0.30;\n"
    "  vec3 amb = mix(gndCol, skyCol, n.y * 0.5 + 0.5);\n"
    "  vec3 c = base * (amb + keyCol * key + keyCol * fill);\n"
    // specular: tight highlight on the key, softened by the gloss term
    "  vec3 h = normalize(l + v);\n"
    "  float spec = pow(max(dot(n, h), 0.0), gloss) * surf * step(0.0, ndl);\n"
    "  c += keyCol * spec;\n"
    // torch point lights, inverse-square with a soft radius cutoff
    "  for (int i = 0; i < 4; i++) {\n"
    "    vec3 dv = ptPos[i] - fragPos;\n"
    "    float dd = dot(dv, dv);\n"
    "    vec3 pl = dv * inversesqrt(max(dd, 0.0001));\n"
    "    float att = 1.0 / (1.0 + dd * 0.10);\n"
    "    float pn = max(dot(n, pl) * 0.7 + 0.3, 0.0);\n"
    "    c += base * ptCol[i] * pn * att;\n"
    "  }\n"
    // rim / fresnel, tinted by the stage's warm key
    "  float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);\n"
    "  c += rimCol * rim * rimAmt;\n"
    "  c += base * emis;\n"
    // height + distance fog pushes the stands and mountains back
    "  float dist = length(viewPos - fragPos);\n"
    "  float fog = 1.0 - exp(-dist * fogAmt * (1.0 - clamp(fragPos.y * 0.035, 0.0, 0.7)));\n"
    "  c = mix(c, fogCol, clamp(fog, 0.0, 0.85));\n"
    "  finalColor = vec4(c, colDiffuse.a);\n"
    "}\n";

static const char *FS_BRIGHT =
    "#version 330\n"
    "in vec2 fragTexCoord; uniform sampler2D texture0;\n"
    "out vec4 finalColor;\n"
    "void main(){\n"
    "  vec3 c = texture(texture0, fragTexCoord).rgb;\n"
    "  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    // soft knee so highlights ramp into the bloom instead of popping
    "  float k = clamp((l - 0.62) / 0.45, 0.0, 1.0);\n"
    "  finalColor = vec4(c * k * k, 1.0);\n"
    "}\n";

static const char *FS_BLUR =
    "#version 330\n"
    "in vec2 fragTexCoord; uniform sampler2D texture0;\n"
    "uniform vec2 dir;           // texel-sized step, horizontal or vertical\n"
    "out vec4 finalColor;\n"
    "void main(){\n"
    "  float w[5] = float[](0.2270, 0.1946, 0.1216, 0.0540, 0.0162);\n"
    "  vec3 c = texture(texture0, fragTexCoord).rgb * w[0];\n"
    "  for (int i = 1; i < 5; i++) {\n"
    "    c += texture(texture0, fragTexCoord + dir * float(i)).rgb * w[i];\n"
    "    c += texture(texture0, fragTexCoord - dir * float(i)).rgb * w[i];\n"
    "  }\n"
    "  finalColor = vec4(c, 1.0);\n"
    "}\n";

static const char *FS_COMPOSITE =
    "#version 330\n"
    "in vec2 fragTexCoord; uniform sampler2D texture0; uniform sampler2D bloomTex;\n"
    "uniform float bloomAmt; uniform float vignette; uniform float aberr;\n"
    "uniform float grain; uniform float time; uniform float flashAmt; uniform vec3 flashCol;\n"
    "out vec4 finalColor;\n"
    "vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }\n"
    "void main(){\n"
    "  vec2 uv = fragTexCoord;\n"
    "  vec2 off = (uv - 0.5);\n"
    "  float r2 = dot(off, off);\n"
    // chromatic aberration grows toward the corners
    "  vec2 ca = off * aberr * r2;\n"
    "  vec3 c;\n"
    "  c.r = texture(texture0, uv + ca).r;\n"
    "  c.g = texture(texture0, uv).g;\n"
    "  c.b = texture(texture0, uv - ca).b;\n"
    "  c += texture(bloomTex, uv).rgb * bloomAmt;\n"
    "  c += flashCol * flashAmt;\n"
    "  c = aces(c * 1.06);\n"
    // grade: lift the shadows cool, push the highlights warm, then add punch
    "  c = mix(c, c * vec3(0.92, 0.96, 1.12), 1.0 - smoothstep(0.0, 0.45, c));\n"
    "  c = mix(c, c * vec3(1.08, 1.01, 0.90), smoothstep(0.55, 1.0, c));\n"
    "  float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "  c = clamp(mix(vec3(lum), c, 1.14), 0.0, 1.0);\n"
    "  c *= mix(1.0, 1.0 - r2 * 1.55, vignette);\n"
    // fine film grain, strongest in the midtones
    "  float g = fract(sin(dot(uv * (1.0 + time), vec2(12.9898, 78.233))) * 43758.5453);\n"
    "  c += (g - 0.5) * grain * (1.0 - abs(lum - 0.5) * 1.2);\n"
    "  finalColor = vec4(c, 1.0);\n"
    "}\n";

// ----------------------------------------------------------------------------
// Input
// ----------------------------------------------------------------------------
static Input ReadHuman(int p) {
    Input in;
    bool left, right;
    if (p == 0) {
        left = IsKeyDown(KEY_A); right = IsKeyDown(KEY_D);
        in.up = IsKeyDown(KEY_W); in.down = IsKeyDown(KEY_S);
        in.ssBg = IsKeyPressed(KEY_Q); in.ssFg = IsKeyPressed(KEY_E);
        in.lp = IsKeyDown(KEY_U); in.rp = IsKeyDown(KEY_I); in.lk = IsKeyDown(KEY_J); in.rk = IsKeyDown(KEY_K);
        in.lpP = IsKeyPressed(KEY_U); in.rpP = IsKeyPressed(KEY_I); in.lkP = IsKeyPressed(KEY_J); in.rkP = IsKeyPressed(KEY_K);
        if (IsGamepadAvailable(0)) {
            left  |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)  || GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X) < -0.5f;
            right |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X) >  0.5f;
            in.up   |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP);
            in.down |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y) > 0.5f;
            in.ssBg |= IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1);
            in.ssFg |= IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1);
            in.lp |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);  in.lpP |= IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
            in.rp |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_UP);    in.rpP |= IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP);
            in.lk |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);  in.lkP |= IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
            in.rk |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT); in.rkP |= IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
        }
        in.fwd = right; in.back = left;          // P1 is always on screen left
    } else {
        left = IsKeyDown(KEY_LEFT); right = IsKeyDown(KEY_RIGHT);
        in.up = IsKeyDown(KEY_UP); in.down = IsKeyDown(KEY_DOWN);
        in.ssBg = IsKeyPressed(KEY_KP_7) || IsKeyPressed(KEY_COMMA);
        in.ssFg = IsKeyPressed(KEY_KP_9) || IsKeyPressed(KEY_PERIOD);
        in.lp = IsKeyDown(KEY_KP_4); in.rp = IsKeyDown(KEY_KP_5); in.lk = IsKeyDown(KEY_KP_1); in.rk = IsKeyDown(KEY_KP_2);
        in.lpP = IsKeyPressed(KEY_KP_4); in.rpP = IsKeyPressed(KEY_KP_5); in.lkP = IsKeyPressed(KEY_KP_1); in.rkP = IsKeyPressed(KEY_KP_2);
        in.fwd = left; in.back = right;          // P2 is always on screen right
    }
    // double tap = dash
    int now = (int)G.frame;
    if (in.fwd && !G.wasFwd[p])  { if (now - G.lastFwdTap[p]  < 14) in.dashF = true; G.lastFwdTap[p]  = now; }
    if (in.back && !G.wasBack[p]) { if (now - G.lastBackTap[p] < 14) in.dashB = true; G.lastBackTap[p] = now; }
    G.wasFwd[p] = in.fwd; G.wasBack[p] = in.back;
    return in;
}

static void ClearPressed(Input &in) { in.lpP = in.rpP = in.lkP = in.rkP = in.ssBg = in.ssFg = in.dashF = in.dashB = false; }

static int ResolveMove(const Input &in) {
    if ((in.lpP && in.lk) || (in.lkP && in.lp)) return M_THROW;
    if (in.down) {
        if (in.lpP) return M_DLP;
        if (in.rpP) return M_DRP;
        if (in.lkP) return M_DLK;
        if (in.rkP) return M_DRK;
    }
    if (in.fwd && in.rpP) return M_FRP;
    if (in.lpP) return M_LP;
    if (in.rpP) return M_RP;
    if (in.lkP) return M_LK;
    if (in.rkP) return M_RK;
    return M_NONE;
}

// ----------------------------------------------------------------------------
// CPU opponent
// ----------------------------------------------------------------------------
static void AIPress(Input &in, int move) {
    switch (move) {
        case M_LP:  in.lp = in.lpP = true; break;
        case M_RP:  in.rp = in.rpP = true; break;
        case M_LK:  in.lk = in.lkP = true; break;
        case M_RK:  in.rk = in.rkP = true; break;
        case M_DLP: in.down = true; in.lp = in.lpP = true; break;
        case M_DRP: in.down = true; in.rp = in.rpP = true; break;
        case M_DLK: in.down = true; in.lk = in.lkP = true; break;
        case M_DRK: in.down = true; in.rk = in.rkP = true; break;
        case M_FRP: in.fwd = true; in.rp = in.rpP = true; break;
        case M_THROW: in.lp = in.lk = in.lpP = in.lkP = true; break;
        default: break;
    }
}

static Input ThinkAI(AIState &ai, const Fighter &me, const Fighter &op, int level) {
    static const float blockP[3] = { 0.28f, 0.55f, 0.82f };
    static const float lowP[3]   = { 0.12f, 0.33f, 0.62f };
    static const int   thinkB[3] = { 42, 26, 14 };
    Input in;
    float dx = op.pos.x - me.pos.x, dz = op.pos.z - me.pos.z;
    float dist = sqrtf(dx * dx + dz * dz);
    bool canAct = me.state == ST_IDLE || me.state == ST_CROUCH;

    if (ai.qBtn && --ai.qDelay <= 0) { AIPress(in, ai.qBtn); ai.qBtn = 0; return in; }

    // react to an incoming attack once per attack
    if (op.state == ST_ATTACK && op.serial != ai.seen) {
        const MoveDef &mv = MOVES[op.move];
        if (dist < mv.range + mv.lunge + 0.5f) {
            ai.seen = op.serial;
            float r = Frand();
            int remain = mv.startup + mv.active - op.moveFrame + 5;
            if (mv.level == LV_THROW) {
                if (r < lowP[level]) { ai.guardT = remain; ai.guardLow = true; ai.guardBack = false; }
            } else if (mv.level == LV_LOW) {
                if (r < lowP[level])        { ai.guardT = remain; ai.guardLow = true;  ai.guardBack = true; }
                else if (r < blockP[level]) { ai.guardT = remain; ai.guardLow = false; ai.guardBack = true; }
            } else {
                if (r < blockP[level])      { ai.guardT = remain; ai.guardLow = false; ai.guardBack = true; }
                else if (r < blockP[level] + 0.10f && canAct) { if (Frand() < 0.5f) in.ssBg = true; else in.ssFg = true; }
            }
        }
    }
    if (ai.guardT > 0) { ai.guardT--; in.back = ai.guardBack; in.down = ai.guardLow; return in; }

    if (ai.walkT > 0) { ai.walkT--; if (ai.walkDir > 0) in.fwd = true; else in.back = true; }
    if (!canAct) return in;
    if (--ai.think > 0) return in;
    ai.think = thinkB[level] + GetRandomValue(0, 20);

    if (op.state == ST_LAUNCH) {                       // juggle
        if (dist < 1.5f && op.pos.y < 1.5f) {
            int pick = GetRandomValue(0, 2);
            AIPress(in, pick == 0 ? M_LP : pick == 1 ? M_RP : M_LK);
            if (pick == 0) { ai.qBtn = M_RP; ai.qDelay = 11; }
            ai.think = 12;
        } else { ai.walkT = 8; ai.walkDir = 1; ai.think = 6; }
        return in;
    }
    if (op.state == ST_DOWN || op.state == ST_GETUP) {
        if (op.state == ST_DOWN && dist < 1.5f && Frand() < 0.45f) AIPress(in, M_DLK);
        else { ai.walkT = 12; ai.walkDir = (dist < 1.6f) ? -1 : 1; }
        return in;
    }
    if (dist > 3.4f) { if (Frand() < 0.4f) in.dashF = true; else { ai.walkT = 28; ai.walkDir = 1; } ai.think = 14; return in; }
    if (dist > 1.9f) {
        float r = Frand();
        if (r < 0.50f) { ai.walkT = 16; ai.walkDir = 1; ai.think = 10; }
        else if (r < 0.65f) AIPress(in, M_FRP);
        else if (r < 0.75f) AIPress(in, M_RK);
        else if (r < 0.85f) { if (Frand() < 0.5f) in.ssBg = true; else in.ssFg = true; }
        else in.dashF = true;
        return in;
    }
    // close range offence
    float r = Frand() * 100;
    if      (r < 24) { AIPress(in, M_LP); if (Frand() < 0.7f) { ai.qBtn = M_RP; ai.qDelay = 12; } }
    else if (r < 41) AIPress(in, M_LK);
    else if (r < 55) AIPress(in, M_DLK);
    else if (r < 66) AIPress(in, M_DRP);
    else if (r < 73) AIPress(in, M_FRP);
    else if (r < 81) { if (dist < 1.1f) AIPress(in, M_THROW); else { ai.walkT = 6; ai.walkDir = 1; ai.think = 7; } }
    else if (r < 87) AIPress(in, M_RK);
    else if (r < 91) AIPress(in, M_DRK);
    else if (r < 95) in.dashB = true;
    else { ai.guardT = 20; ai.guardLow = false; ai.guardBack = true; }
    return in;
}

// ----------------------------------------------------------------------------
// Poses
// ----------------------------------------------------------------------------
static void Plant(Pose &p, bool left, bool right) {
    float y = -(p.hipY - 0.05f) + 0.06f;
    if (left)  p.lFoot.y = y;
    if (right) p.rFoot.y = y;
}

static Pose BasePose(bool crouch, float t) {
    Pose p;
    if (!crouch) {
        float bob = sinf(t * 3.5f) * 0.012f;
        p.hipY = 0.92f + bob; p.lean = 0.12f;
        p.lHand = { 0.34f, -0.02f + bob, 0.08f }; p.rHand = { 0.22f, -0.12f - bob, -0.06f };
        p.lFoot = { 0.24f, 0, -0.04f };           p.rFoot = { -0.24f, 0, 0.06f };
    } else {
        p.hipY = 0.58f; p.lean = 0.38f;
        p.lHand = { 0.30f, 0.02f, 0.08f }; p.rHand = { 0.22f, -0.04f, -0.06f };
        p.lFoot = { 0.28f, 0, -0.06f };    p.rFoot = { -0.22f, 0, 0.08f };
    }
    Plant(p, true, true);
    return p;
}

static Pose TargetPose(const Fighter &f, float t) {
    Pose p = BasePose(f.crouched, t + f.id * 1.7f);
    switch (f.state) {
    case ST_IDLE:
        if (f.walking) {
            float s = sinf(f.walkPhase), c = cosf(f.walkPhase);
            p.lFoot.x += s * 0.11f * f.walkSign; p.rFoot.x -= s * 0.11f * f.walkSign;
            p.lFoot.y += fmaxf(0, c) * 0.06f;    p.rFoot.y += fmaxf(0, -c) * 0.06f;
            if (f.in.back) { p.lHand = { 0.26f, 0.10f, 0.10f }; p.rHand = { 0.20f, 0.06f, -0.10f }; p.lean = 0.02f; }
        }
        break;
    case ST_CROUCH: break;
    case ST_DASH:
        p.lean = f.dashDir > 0 ? 0.40f : -0.18f; p.hipY = 0.86f;
        p.lFoot.x = f.dashDir > 0 ? 0.40f : 0.10f; p.rFoot.x = f.dashDir > 0 ? -0.10f : -0.42f;
        Plant(p, true, true);
        break;
    case ST_SIDESTEP:
        p.hipY = 0.84f; p.lFoot.z = -0.26f; p.rFoot.z = 0.26f; p.lFoot.x = 0.10f; p.rFoot.x = -0.10f;
        Plant(p, true, true);
        break;
    case ST_JUMP:
        p.hipY = 0.95f; p.lean = 0.2f;
        p.lFoot = { 0.22f, -0.55f, -0.05f }; p.rFoot = { -0.08f, -0.60f, 0.05f };
        break;
    case ST_ATTACK: {
        const MoveDef &mv = MOVES[f.move];
        int fr = f.moveFrame;
        bool wind = fr <= (int)(mv.startup * 0.55f);
        bool strike = !wind && fr <= mv.startup + mv.active + 4;
        if (wind || strike) {
            Vector3 tgt = wind ? mv.windup : mv.strike;
            p.hipY = wind ? mv.hipYWind : mv.hipY;
            p.lean = wind ? mv.leanWind : mv.leanStrike;
            bool kickL = mv.limb == L_FOOT, kickR = mv.limb == R_FOOT;
            if (kickL) p.rFoot.x = -0.08f;
            if (kickR) p.lFoot.x = 0.05f;
            Plant(p, !kickL, !kickR);
            switch (mv.limb) {
                case L_HAND: p.lHand = tgt; p.rHand = { 0.18f, 0.02f, -0.08f }; break;
                case R_HAND: p.rHand = tgt; p.lHand = { 0.20f, 0.04f, 0.08f }; break;
                case L_FOOT: p.lFoot = tgt; p.lHand = { 0.22f, 0.05f, 0.05f }; p.rHand = { 0.05f, -0.20f, 0.05f }; break;
                case R_FOOT: p.rFoot = tgt; p.lHand = { 0.22f, 0.05f, 0.05f }; p.rHand = { 0.00f, -0.22f, 0.08f }; break;
                case BOTH_HANDS: p.lHand = { tgt.x, tgt.y, 0.12f }; p.rHand = { tgt.x, tgt.y, -0.12f }; break;
            }
        }
        break; }
    case ST_BLOCK:
        p.lean = f.crouched ? 0.25f : -0.12f;
        p.lHand = { 0.24f, 0.14f, 0.12f }; p.rHand = { 0.20f, 0.10f, -0.12f };
        break;
    case ST_HIT:
        p.hipY = 0.88f; p.lean = -0.45f;
        p.lHand = { 0.10f, -0.30f, -0.10f }; p.rHand = { 0.05f, -0.32f, 0.10f };
        Plant(p, true, true);
        break;
    case ST_LAUNCH: case ST_THROWN:
        p.hipY = f.state == ST_THROWN ? 0.7f : 0.5f;
        p.tilt = f.state == ST_THROWN ? 1.9f : 1.3f; p.lean = -0.15f;
        p.lHand = { 0.25f, 0.15f, -0.20f }; p.rHand = { 0.25f, 0.10f, 0.20f };
        p.lFoot = { 0.30f, -0.75f, -0.12f }; p.rFoot = { 0.12f, -0.85f, 0.12f };
        break;
    case ST_DOWN: case ST_KO:
        p.hipY = 0.17f; p.tilt = PI * 0.5f; p.lean = 0.0f;
        p.lHand = { -0.10f, -0.35f, -0.22f }; p.rHand = { -0.08f, -0.38f, 0.22f };
        p.lFoot = { -0.10f, -0.90f, -0.12f }; p.rFoot = { 0.12f, -0.80f, 0.14f };
        break;
    case ST_GETUP:
        p.hipY = 0.50f; p.lean = 0.65f; p.tilt = 0;
        p.lHand = { 0.30f, -0.45f, 0.0f }; p.rHand = { 0.25f, -0.45f, 0.0f };
        Plant(p, true, true);
        break;
    case ST_THROWING:
        if (f.moveFrame < 14)      { p.lHand = { 0.48f, 0.00f, 0.12f }; p.rHand = { 0.48f, 0.00f, -0.12f }; p.lean = 0.30f; }
        else if (f.moveFrame < 30) { p.lHand = { 0.20f, 0.50f, 0.10f }; p.rHand = { 0.20f, 0.50f, -0.10f }; p.lean = -0.25f; }
        else                       { p.lHand = { 0.50f, -0.40f, 0.10f }; p.rHand = { 0.50f, -0.40f, -0.10f }; p.lean = 0.55f; p.hipY = 0.8f; Plant(p, true, true); }
        break;
    case ST_WIN:
        p.lean = -0.05f;
        p.lHand = { 0.05f, 0.55f, -0.08f }; p.rHand = { 0.10f, -0.15f, 0.05f };
        break;
    }
    return p;
}

static void BlendPose(Pose &a, const Pose &b, float k) {
    a.hipY = Lerp(a.hipY, b.hipY, k); a.lean = Lerp(a.lean, b.lean, k); a.tilt = Lerp(a.tilt, b.tilt, k);
    a.lHand = Vector3Lerp(a.lHand, b.lHand, k); a.rHand = Vector3Lerp(a.rHand, b.rHand, k);
    a.lFoot = Vector3Lerp(a.lFoot, b.lFoot, k); a.rFoot = Vector3Lerp(a.rFoot, b.rFoot, k);
}

// ----------------------------------------------------------------------------
// Combat
// ----------------------------------------------------------------------------
static void DealDamage(Fighter &a, Fighter &d, int dmg) {
    d.hp -= dmg;
    d.flash = 1.0f;
    d.comboCount++; d.comboDmg += dmg;
    a.showCount = d.comboCount; a.showDmg = d.comboDmg; a.showT = 85;
    if (d.hp <= 0) { d.hp = 0; d.ko = true; }
}

static void StartMove(Fighter &f, float wantYaw, int m, int skip) {
    f.yaw = wantYaw;
    f.state = ST_ATTACK; f.move = m; f.moveFrame = skip; f.moveHit = false; f.serial++;
    f.crouched = MOVES[m].crouching;
    f.buf = 0; f.bufT = 0;
}

static int ActiveMove(const Fighter &f) {
    if (f.state != ST_ATTACK || f.moveHit) return 0;
    const MoveDef &mv = MOVES[f.move];
    return (f.moveFrame > mv.startup && f.moveFrame <= mv.startup + mv.active) ? f.move : 0;
}

static void ResolveHit(Fighter &a, Fighter &d, int moveId) {
    const MoveDef &mv = MOVES[moveId];
    if (d.state == ST_KO || d.state == ST_GETUP || d.state == ST_THROWN || d.state == ST_THROWING || d.state == ST_WIN) return;
    if (a.state == ST_THROWN || a.state == ST_THROWING) return;

    Vector3 fw = Fwd(a), sd = Side(a);
    Vector3 rel = { d.pos.x - a.pos.x, 0, d.pos.z - a.pos.z };
    float dx = Vector3DotProduct(rel, fw), dz = Vector3DotProduct(rel, sd);
    if (dx < -0.2f || dx > mv.range || fabsf(dz) > 0.5f) return;      // out of reach, or sidestepped

    bool air = d.state == ST_LAUNCH || d.state == ST_JUMP;
    bool crouch = d.crouched && (d.state == ST_CROUCH || d.state == ST_ATTACK || d.state == ST_BLOCK);

    if (mv.level == LV_THROW) {
        if (air || crouch || d.state == ST_HIT || d.state == ST_BLOCK || d.state == ST_DOWN) return;
        a.moveHit = true;
        a.state = ST_THROWING; a.moveFrame = 0;
        d.state = ST_THROWN; d.crouched = false; d.kb = { 0, 0, 0 };
        SetMsg(a, "THROW");
        Play(SND_WHOOSH);
        return;
    }
    if (mv.level == LV_HIGH && crouch) return;
    if (d.state == ST_DOWN && mv.level != LV_LOW) return;
    if (air && mv.level == LV_LOW && d.pos.y > 0.25f) return;
    if (air && d.pos.y > 1.7f) return;

    a.moveHit = true;
    float len = Vector3Length(rel);
    Vector3 dir = len > 0.01f ? VMul(rel, 1 / len) : fw;
    float h = mv.level == LV_HIGH ? 1.45f : mv.level == LV_MID ? 1.05f : 0.30f;
    if (air) h = d.pos.y + 0.6f;
    if (d.state == ST_DOWN) h = 0.2f;
    Vector3 sp = VAdd(a.pos, VMul(fw, fminf(dx, mv.range) * 0.85f)); sp.y = h;

    // guard check: hold back. Standing guard stops high + mid, crouching guard stops low.
    bool guarding = (d.state == ST_IDLE || d.state == ST_CROUCH || d.state == ST_BLOCK) && d.in.back && !d.in.fwd;
    bool blocked = guarding && (mv.level == LV_LOW ? d.crouched : !d.crouched);
    if (blocked) {
        d.state = ST_BLOCK; d.timer = mv.blockstun;
        d.kb = VMul(dir, mv.pushBlock); a.kb = VMul(dir, -mv.pushBlock * 0.4f);
        SpawnSparks(sp, { 170, 210, 255, 255 }, 6, 0.04f);
        G.hitstop = 3; Play(SND_BLOCK);
        return;
    }

    int dmg = mv.damage;
    bool counter = d.state == ST_ATTACK;
    if (counter) { dmg = dmg * 12 / 10 + 1; SetMsg(a, "COUNTER"); }
    bool heavy = dmg >= 13;

    if (d.state == ST_DOWN) {                              // ground hit
        DealDamage(a, d, std::max(1, dmg / 2));
        if (d.ko) { d.state = ST_KO; }
    } else {
        if (d.state == ST_IDLE || d.state == ST_CROUCH || d.state == ST_ATTACK || d.state == ST_DASH ||
            d.state == ST_SIDESTEP || d.state == ST_JUMP) { d.comboCount = 0; d.comboDmg = 0; }
        if (d.state == ST_LAUNCH) {
            static const float scale[5] = { 1.0f, 0.8f, 0.65f, 0.5f, 0.4f };
            dmg = std::max(1, (int)(dmg * scale[std::min(d.juggle, 4)]));
        }
        DealDamage(a, d, dmg);
        d.crouched = false;
        if (d.ko) {
            d.state = ST_LAUNCH; d.vy = 0.11f; d.kb = VMul(dir, 0.07f);
        } else if (air) {
            d.state = ST_LAUNCH;
            d.vy = fmaxf(0.04f, 0.085f - 0.012f * d.juggle);
            d.kb = VMul(dir, 0.045f); d.juggle++;
        } else if (mv.effect == FX_LAUNCH) {
            d.state = ST_LAUNCH; d.vy = 0.15f; d.kb = VMul(dir, 0.022f); d.juggle = 1; SetMsg(a, "LAUNCH");
        } else if (mv.effect == FX_KNOCKDOWN) {
            d.state = ST_LAUNCH; d.vy = 0.08f; d.kb = VMul(dir, 0.085f); d.juggle = 2;
        } else if (mv.effect == FX_SWEEP) {
            d.state = ST_LAUNCH; d.vy = 0.06f; d.kb = VMul(dir, 0.02f); d.juggle = 2;
        } else {
            d.state = ST_HIT; d.timer = mv.hitstun + (counter ? 4 : 0); d.kb = VMul(dir, mv.pushHit);
        }
    }
    SpawnSparks(sp, heavy ? Color{ 255, 170, 60, 255 } : Color{ 255, 235, 140, 255 }, heavy ? 16 : 9, heavy ? 0.075f : 0.05f);
    G.hitstop = heavy ? 8 : 5;
    G.shake = fmaxf(G.shake, heavy ? 0.12f : 0.05f);
    G.camPunch = fmaxf(G.camPunch, heavy ? 0.55f : 0.22f);
    G.camRoll += (heavy ? 0.045f : 0.018f) * (a.id == 0 ? 1.0f : -1.0f);
    G.screenFlash = fmaxf(G.screenFlash, heavy ? 0.16f : 0.07f);
    G.flashCol = { 1.0f, 0.82f, 0.55f };
    Play(heavy ? SND_HEAVY : SND_HIT);
    if (d.ko) {
        G.slowmo = 96; G.hitstop = 14; G.shake = 0.25f;
        G.camPunch = 0.95f; G.screenFlash = 0.40f; G.flashCol = { 1.0f, 0.65f, 0.45f };
        Play(SND_KO);
    }
}

static void TurnToward(Fighter &f, float want, float rate) {
    float d = AngleDiff(f.yaw, want);
    f.yaw += Clamp(d, -rate, rate);
}

static void UpdateFighter(Fighter &f, Fighter &o, const Input &in) {
    f.in = in;
    Vector3 rel = { o.pos.x - f.pos.x, 0, o.pos.z - f.pos.z };
    float dist = Vector3Length(rel);
    Vector3 dir = dist > 0.001f ? VMul(rel, 1 / dist) : Fwd(f);
    float wantYaw = atan2f(-dir.z, dir.x);

    int m = ResolveMove(in);
    if (m) { f.buf = m; f.bufT = 9; }
    else if (f.bufT > 0 && --f.bufT == 0) f.buf = 0;

    f.walking = false;
    f.flash *= 0.85f;
    if (f.msgT > 0) f.msgT--;
    if (f.showT > 0) f.showT--;
    f.hpLag = f.hpLag > f.hp ? fmaxf((float)f.hp, f.hpLag - 0.45f) : (float)f.hp;

    switch (f.state) {
    case ST_IDLE: case ST_CROUCH:
        TurnToward(f, wantYaw, 0.3f);
        f.comboCount = 0; f.comboDmg = 0; f.juggle = 0;
        if (f.buf) { StartMove(f, wantYaw, f.buf, 0); break; }
        if (in.ssBg || in.ssFg) { f.state = ST_SIDESTEP; f.timer = 14; f.ssDir = in.ssBg ? 1 : -1; f.crouched = false; break; }
        if (in.dashF) { f.state = ST_DASH; f.timer = 12; f.dashDir = 1;  f.crouched = false; break; }
        if (in.dashB) { f.state = ST_DASH; f.timer = 14; f.dashDir = -1; f.crouched = false; break; }
        if (in.up) {
            f.state = ST_JUMP; f.vy = 0.14f; f.crouched = false;
            f.jumpVx = in.fwd ? 0.05f : in.back ? -0.045f : 0.0f;
            break;
        }
        if (in.down) { f.state = ST_CROUCH; f.crouched = true; }
        else {
            f.state = ST_IDLE; f.crouched = false;
            if (in.fwd && !in.back) {
                if (dist > BODY_DIST + 0.05f) f.pos = VAdd(f.pos, VMul(dir, 0.048f));
                f.walking = true; f.walkSign = 1; f.walkPhase += 0.26f;
            } else if (in.back && !in.fwd) {
                f.pos = VAdd(f.pos, VMul(dir, -0.036f));
                f.walking = true; f.walkSign = -1; f.walkPhase += 0.22f;
            }
        }
        break;

    case ST_DASH:
        TurnToward(f, wantYaw, 0.3f);
        if (f.dashDir > 0) { if (dist > BODY_DIST + 0.05f) f.pos = VAdd(f.pos, VMul(dir, 0.12f)); }
        else f.pos = VAdd(f.pos, VMul(dir, -0.10f));
        if (f.dashDir > 0 && f.buf && f.timer < 8) { StartMove(f, wantYaw, f.buf, 0); break; }
        if (--f.timer <= 0) f.state = ST_IDLE;
        break;

    case ST_SIDESTEP:
        TurnToward(f, wantYaw, 0.35f);
        f.pos = VAdd(f.pos, VMul(G.bgDir, f.ssDir * 0.082f));
        if (--f.timer <= 0) f.state = ST_IDLE;
        break;

    case ST_JUMP:
        f.pos = VAdd(f.pos, VMul(Fwd(f), f.jumpVx));
        f.pos.y += f.vy; f.vy -= 0.0115f;
        if (f.pos.y <= 0 && f.vy < 0) { f.pos.y = 0; f.state = ST_IDLE; }
        break;

    case ST_ATTACK: {
        const MoveDef &mv = MOVES[f.move];
        // late throw input (LP then LK one frame apart)
        if (f.moveFrame <= 3 && f.buf == M_THROW && f.move != M_THROW) { StartMove(f, f.yaw, M_THROW, f.moveFrame); break; }
        f.moveFrame++;
        if (f.moveFrame <= mv.startup && dist > BODY_DIST) f.pos = VAdd(f.pos, VMul(Fwd(f), mv.lunge / mv.startup));
        if (f.moveFrame == mv.startup) Play(SND_WHOOSH);
        // strings: LP -> RP -> LK
        if (f.moveFrame > mv.startup + 1) {
            if (f.move == M_LP && f.buf == M_RP) { StartMove(f, f.yaw, M_RP, 4); break; }
            if (f.move == M_RP && f.buf == M_LK) { StartMove(f, f.yaw, M_LK, 4); break; }
        }
        if (f.moveFrame >= mv.startup + mv.active + mv.recovery) {
            f.state = (mv.crouching && in.down) ? ST_CROUCH : ST_IDLE;
            f.crouched = f.state == ST_CROUCH;
        }
        break; }

    case ST_HIT:
        if (--f.timer <= 0) f.state = ST_IDLE;
        break;

    case ST_BLOCK:
        TurnToward(f, wantYaw, 0.3f);
        f.crouched = in.down;
        if (--f.timer <= 0) f.state = f.crouched ? ST_CROUCH : ST_IDLE;
        break;

    case ST_LAUNCH:
        f.pos.y += f.vy;
        f.vy -= 0.0068f + 0.0012f * f.juggle;
        if (f.pos.y <= 0 && f.vy < 0) {
            f.pos.y = 0; f.kb = VMul(f.kb, 0.4f);
            f.state = f.ko ? ST_KO : ST_DOWN; f.timer = 36;
            SpawnSparks({ f.pos.x, 0.05f, f.pos.z }, { 150, 140, 130, 255 }, 8, 0.03f);
            G.flashes.pop_back();
            Play(SND_THUD);
        }
        break;

    case ST_DOWN:
        if (--f.timer <= 0) { f.state = ST_GETUP; f.timer = 22; }
        break;

    case ST_GETUP:
        TurnToward(f, wantYaw, 0.3f);
        if (--f.timer <= 0) { f.state = ST_IDLE; f.juggle = 0; }
        break;

    case ST_THROWING: {
        int t = f.moveFrame++;
        Vector3 fw = Fwd(f);
        if (o.state == ST_THROWN) {
            float k = t / 30.0f;
            o.pos = VAdd(f.pos, VMul(fw, 0.55f + 0.75f * k));
            o.pos.y = sinf(k * PI) * 1.15f;
            if (t >= 30) {
                o.pos.y = 0;
                o.comboCount = 0; o.comboDmg = 0;
                DealDamage(f, o, MOVES[M_THROW].damage);
                o.state = o.ko ? ST_KO : ST_DOWN; o.timer = 45; o.kb = VMul(fw, 0.05f);
                SpawnSparks({ o.pos.x, 0.1f, o.pos.z }, { 255, 170, 60, 255 }, 18, 0.08f);
                G.shake = 0.22f; G.hitstop = 9; Play(SND_HEAVY);
                if (o.ko) { G.slowmo = 96; Play(SND_KO); }
            }
        }
        if (t >= 48) f.state = ST_IDLE;
        break; }

    case ST_THROWN: case ST_KO: case ST_WIN: break;
    }

    f.pos.x += f.kb.x; f.pos.z += f.kb.z;
    if (f.state != ST_LAUNCH) f.kb = VMul(f.kb, 0.8f);
}

static void SeparateBodies(Fighter &a, Fighter &b) {
    if (a.state == ST_THROWN || b.state == ST_THROWN) return;
    if (a.pos.y > 0.5f || b.pos.y > 0.5f) return;
    Vector3 rel = { b.pos.x - a.pos.x, 0, b.pos.z - a.pos.z };
    float d = Vector3Length(rel);
    if (d >= BODY_DIST) return;
    Vector3 n = d > 0.001f ? VMul(rel, 1 / d) : Fwd(a);
    float push = (BODY_DIST - d) * 0.5f;
    a.pos = VAdd(a.pos, VMul(n, -push));
    b.pos = VAdd(b.pos, VMul(n, push));
}

static void ClampArena(Fighter &f) {
    float r = sqrtf(f.pos.x * f.pos.x + f.pos.z * f.pos.z);
    if (r > ARENA_R) { f.pos.x *= ARENA_R / r; f.pos.z *= ARENA_R / r; }
}

// ----------------------------------------------------------------------------
// Match flow
// ----------------------------------------------------------------------------
static void ResetFighter(Fighter &f, int id) {
    int wins = f.wins;
    Fighter n;
    n.id = id; n.wins = wins;
    if (id == 0) {
        n.name = "KAIDEN"; n.skin = { 222, 170, 130, 255 }; n.cloth = { 235, 235, 240, 255 };
        n.accent = { 200, 40, 40, 255 }; n.hair = { 30, 25, 25, 255 };
        n.pos = { -1.5f, 0, 0 }; n.yaw = 0;
    } else {
        n.name = "VOLKOV"; n.skin = { 205, 165, 140, 255 }; n.cloth = { 35, 45, 80, 255 };
        n.accent = { 235, 180, 50, 255 }; n.hair = { 210, 200, 180, 255 };
        n.pos = { 1.5f, 0, 0 }; n.yaw = PI;
    }
    n.pose = BasePose(false, 0);
    f = n;
}

static void StartRound() {
    ResetFighter(G.f[0], 0); ResetFighter(G.f[1], 1);
    G.ai[0] = AIState(); G.ai[1] = AIState();
    G.timer = ROUND_FRAMES; G.phase = PH_INTRO; G.phaseT = 120;
    G.hitstop = 0; G.slowmo = 0; G.winner = -1;
    G.parts.clear(); G.flashes.clear();
    bool final = G.f[0].wins == ROUNDS_TO_WIN - 1 && G.f[1].wins == ROUNDS_TO_WIN - 1;
    SetBanner(final ? "FINAL ROUND" : TextFormat("ROUND %d", G.round), 80);
}

static void StartMatch() { G.f[0].wins = G.f[1].wins = 0; G.round = 1; StartRound(); }

static void EndRound(int winner, const char *text) {
    G.phase = PH_ROUND_END; G.phaseT = 210; G.winner = winner;
    if (winner >= 0) G.f[winner].wins++;
    else { G.f[0].wins = std::min(G.f[0].wins + 1, ROUNDS_TO_WIN - 1); G.f[1].wins = std::min(G.f[1].wins + 1, ROUNDS_TO_WIN - 1); }
    SetBanner(text, 110);
}

static void UpdateCamera3D(bool orbit) {
    Fighter &a = G.f[0], &b = G.f[1];
    Vector3 axis = { b.pos.x - a.pos.x, 0, b.pos.z - a.pos.z };
    float sep = Vector3Length(axis);
    if (sep > 0.05f) axis = VMul(axis, 1 / sep); else axis = { 1, 0, 0 };
    Vector3 perp = { -axis.z, 0, axis.x };           // camera sits here so P1 stays screen-left
    G.bgDir = { axis.z, 0, -axis.x };
    if (orbit) G.camAngle += 0.004f;
    else G.camAngle += AngleDiff(G.camAngle, atan2f(perp.z, perp.x)) * 0.07f;
    float wantDist = Clamp(4.6f + sep * 0.8f, 5.2f, 12.0f);
    G.camDist = Lerp(G.camDist, wantDist, 0.08f);
    float air = fmaxf(a.pos.y, b.pos.y);
    Vector3 tgt = { (a.pos.x + b.pos.x) * 0.5f, 1.0f + air * 0.3f, (a.pos.z + b.pos.z) * 0.5f };
    G.camTarget = Vector3Lerp(G.camTarget, tgt, 0.15f);
}

static void StepParticles() {
    for (auto &p : G.parts) { p.p = VAdd(p.p, p.v); p.v.y -= 0.004f; p.life -= 0.04f; if (p.p.y < 0.02f) { p.p.y = 0.02f; p.v.y *= -0.4f; } }
    G.parts.erase(std::remove_if(G.parts.begin(), G.parts.end(), [](const Particle &p) { return p.life <= 0; }), G.parts.end());
    for (auto &f : G.flashes) f.t += 0.12f;
    G.flashes.erase(std::remove_if(G.flashes.begin(), G.flashes.end(), [](const Flash &f) { return f.t >= 1; }), G.flashes.end());
}

static void StepFight(Input in0, Input in1, bool live) {
    G.shake *= 0.86f;
    G.camRoll *= 0.88f;
    G.camPunch *= 0.84f;
    G.screenFlash *= 0.80f;
    if (G.bannerT > 0) G.bannerT--;
    if (G.hitstop > 0) { G.hitstop--; StepParticles(); return; }
    if (G.slowmo > 0) { G.slowmo--; if (G.slowmo % 3 != 0) return; }

    if (!live) { in0 = Input(); in1 = Input(); }
    UpdateFighter(G.f[0], G.f[1], in0);
    UpdateFighter(G.f[1], G.f[0], in1);
    SeparateBodies(G.f[0], G.f[1]);
    int m0 = ActiveMove(G.f[0]), m1 = ActiveMove(G.f[1]);
    if (m0) ResolveHit(G.f[0], G.f[1], m0);
    if (m1) ResolveHit(G.f[1], G.f[0], m1);
    ClampArena(G.f[0]); ClampArena(G.f[1]);

    float t = (float)GetTime();
    for (auto &f : G.f) {
        float k = f.state == ST_ATTACK ? 0.38f : (f.state == ST_HIT || f.state == ST_BLOCK) ? 0.34f : 0.2f;
        BlendPose(f.pose, TargetPose(f, t), k);
        f.breath += (f.state == ST_IDLE || f.state == ST_CROUCH) ? 0.055f : 0.085f;
        UpdateTrail(f);
        // sweat beads off a fighter who is hurt and working
        f.sweat = 1.0f - (float)f.hp / MAX_HP;
        if (f.sweat > 0.45f && f.state != ST_DOWN && f.state != ST_KO && Frand() < f.sweat * 0.10f) {
            Vector3 hp = LimbWorld(f, BOTH_HANDS);
            Particle q;
            q.p = { hp.x + FrandS() * 0.15f, f.pos.y + 1.30f + FrandS() * 0.12f, hp.z + FrandS() * 0.15f };
            q.v = { FrandS() * 0.004f, 0.004f, FrandS() * 0.004f };
            q.life = 0.5f; q.size = 0.018f; q.c = { 190, 215, 235, 255 };
            G.parts.push_back(q);
        }
    }
    StepParticles();
}

static void Step(bool firstStepOfFrame, Input in0, Input in1) {
    if (!firstStepOfFrame) { ClearPressed(in0); ClearPressed(in1); }
    switch (G.phase) {
    case PH_TITLE:
        StepFight(Input(), Input(), false);
        UpdateCamera3D(true);
        break;
    case PH_INTRO:
        StepFight(in0, in1, false);
        UpdateCamera3D(false);
        if (--G.phaseT <= 0) { G.phase = PH_FIGHT; SetBanner("FIGHT!", 45); Play(SND_BELL); }
        break;
    case PH_FIGHT:
        if (G.paused) break;
        StepFight(in0, in1, true);
        UpdateCamera3D(false);
        if (G.hitstop == 0 && G.timer > 0) G.timer--;
        if (G.f[0].ko || G.f[1].ko) {
            if (G.f[0].ko && G.f[1].ko) EndRound(-1, "DOUBLE K.O.");
            else EndRound(G.f[0].ko ? 1 : 0, "K.O.");
        } else if (G.timer <= 0) {
            if (G.f[0].hp == G.f[1].hp) EndRound(-1, "DRAW");
            else EndRound(G.f[0].hp > G.f[1].hp ? 0 : 1, "TIME UP");
        }
        break;
    case PH_ROUND_END:
        StepFight(in0, in1, false);
        UpdateCamera3D(false);
        if (G.winner >= 0 && G.phaseT < 140) {
            Fighter &w = G.f[G.winner];
            if (w.state == ST_IDLE || w.state == ST_CROUCH) { w.state = ST_WIN; w.crouched = false; }
        }
        if (G.phaseT == 100 && G.winner >= 0) SetBanner(TextFormat("%s WINS", G.f[G.winner].name), 95);
        if (--G.phaseT <= 0) {
            if (G.f[0].wins >= ROUNDS_TO_WIN || G.f[1].wins >= ROUNDS_TO_WIN) G.phase = PH_MATCH_END;
            else { G.round++; StartRound(); }
        }
        break;
    case PH_MATCH_END:
        StepFight(Input(), Input(), false);
        UpdateCamera3D(true);
        break;
    }
}

// ----------------------------------------------------------------------------
// Rendering: fighters
// ----------------------------------------------------------------------------
// Surface response for the next draws: specular strength, highlight tightness,
// self-illumination, rim weight. Skipped during the shadow pass, which runs on
// the unlit shader.
static void Surf(float spec, float gloss, float emis, float rim) {
    if (G.shadowPass) return;
    SetShaderValue(G.lit, G.locSurf, &spec, SHADER_UNIFORM_FLOAT);
    SetShaderValue(G.lit, G.locGloss, &gloss, SHADER_UNIFORM_FLOAT);
    SetShaderValue(G.lit, G.locEmis, &emis, SHADER_UNIFORM_FLOAT);
    SetShaderValue(G.lit, G.locRimAmt, &rim, SHADER_UNIFORM_FLOAT);
}
static void SurfSkin()   { Surf(0.28f, 30.0f, 0.00f, 0.34f); }
static void SurfCloth()  { Surf(0.05f, 10.0f, 0.00f, 0.16f); }
static void SurfLeather(){ Surf(0.55f, 54.0f, 0.00f, 0.26f); }
static void SurfStone()  { Surf(0.07f, 12.0f, 0.00f, 0.08f); }
static void SurfMetal()  { Surf(0.85f, 80.0f, 0.00f, 0.40f); }
static void SurfGlow(float e) { Surf(0.0f, 8.0f, e, 0.0f); }

static const Color SHADOW_COL = { 6, 4, 12, 150 };

static void DrawBall(const Matrix &parent, Vector3 c, float r, Color col) {
    Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(r, r, r), MatrixTranslate(c.x, c.y, c.z)), parent);
    G.mat.maps[MATERIAL_MAP_DIFFUSE].color = G.shadowPass ? SHADOW_COL : col;
    DrawMesh(G.sphere, G.mat, m);
}

// Squashed sphere - used for pecs, jaw, boots, anything that shouldn't read as a ball.
static void DrawBlob(const Matrix &parent, Vector3 c, Vector3 r, float yawRad, Color col) {
    Matrix m = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(r.x, r.y, r.z), MatrixRotateY(yawRad)),
                                             MatrixTranslate(c.x, c.y, c.z)), parent);
    G.mat.maps[MATERIAL_MAP_DIFFUSE].color = G.shadowPass ? SHADOW_COL : col;
    DrawMesh(G.sphere, G.mat, m);
}

static Matrix LimbMatrix(Vector3 a, Vector3 b, float r0, float r1) {
    Vector3 d = Vector3Subtract(b, a);
    float len = Vector3Length(d);
    if (len < 0.0001f) return MatrixScale(0, 0, 0);
    Vector3 n = VMul(d, 1 / len);
    Matrix rot;
    if (n.y > 0.9999f) rot = MatrixIdentity();
    else if (n.y < -0.9999f) rot = MatrixRotateX(PI);
    else rot = MatrixRotate(Vector3CrossProduct({ 0, 1, 0 }, n), acosf(n.y));
    (void)r1;
    return MatrixMultiply(MatrixMultiply(MatrixScale(r0, len, r0), rot), MatrixTranslate(a.x, a.y, a.z));
}

static void DrawLimb(const Matrix &parent, Vector3 a, Vector3 b, float r, Color col) {
    Vector3 d = Vector3Subtract(b, a);
    if (Vector3Length(d) < 0.0001f) return;
    G.mat.maps[MATERIAL_MAP_DIFFUSE].color = G.shadowPass ? SHADOW_COL : col;
    DrawMesh(G.cyl, G.mat, MatrixMultiply(LimbMatrix(a, b, r, r), parent));
}

// Tapered segment: a cone-shaped limb, thick at a, thinner at b. Reads as a
// forearm or calf instead of a pipe.
static void DrawTaper(const Matrix &parent, Vector3 a, Vector3 b, float r0, float r1, Color col) {
    Vector3 d = Vector3Subtract(b, a);
    float len = Vector3Length(d);
    if (len < 0.0001f) return;
    Vector3 n = VMul(d, 1 / len);
    Matrix rot;
    if (n.y > 0.9999f) rot = MatrixIdentity();
    else if (n.y < -0.9999f) rot = MatrixRotateX(PI);
    else rot = MatrixRotate(Vector3CrossProduct({ 0, 1, 0 }, n), acosf(n.y));
    // G.cone is a unit cone: radius 1 at the base, apex at y = 1. Scaling it to
    // the ratio r1/r0 and clipping is overkill, so stack two short cylinders.
    int steps = 3;
    for (int i = 0; i < steps; i++) {
        float t0 = (float)i / steps, t1 = (float)(i + 1) / steps;
        Vector3 p0 = VAdd(a, VMul(d, t0)), p1 = VAdd(a, VMul(d, t1));
        float rr = Lerp(r0, r1, (t0 + t1) * 0.5f);
        G.mat.maps[MATERIAL_MAP_DIFFUSE].color = G.shadowPass ? SHADOW_COL : col;
        DrawMesh(G.cyl, G.mat, MatrixMultiply(LimbMatrix(p0, p1, rr, rr), parent));
    }
    (void)rot;
}

static void DrawBox(Vector3 c, Vector3 size, float yawRad, Color col) {
    Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(size.x, size.y, size.z), MatrixRotateY(yawRad)), MatrixTranslate(c.x, c.y, c.z));
    G.mat.maps[MATERIAL_MAP_DIFFUSE].color = G.shadowPass ? SHADOW_COL : col;
    DrawMesh(G.cube, G.mat, m);
}

// two-bone IK: returns the middle joint, clamps the end point to reach
static Vector3 SolveJoint(Vector3 a, Vector3 &b, float l1, float l2, Vector3 bend) {
    Vector3 d = Vector3Subtract(b, a);
    float dist = Vector3Length(d);
    float maxd = (l1 + l2) * 0.999f;
    if (dist < 0.02f) { d = { 0.02f, 0, 0 }; dist = 0.02f; }
    if (dist > maxd) { b = VAdd(a, VMul(d, maxd / dist)); d = Vector3Subtract(b, a); dist = maxd; }
    Vector3 n = VMul(d, 1 / dist);
    float x = (l1 * l1 - l2 * l2 + dist * dist) / (2 * dist);
    float h = sqrtf(fmaxf(0, l1 * l1 - x * x));
    Vector3 bp = Vector3Subtract(bend, VMul(n, Vector3DotProduct(bend, n)));
    if (Vector3Length(bp) < 0.001f) bp = { 0, 1, 0 };
    bp = Vector3Normalize(bp);
    return VAdd(VAdd(a, VMul(n, x)), VMul(bp, h));
}

static Matrix FighterRoot(const Fighter &f) {
    const Pose &p = f.pose;
    return MatrixMultiply(MatrixMultiply(MatrixRotateZ(p.tilt), MatrixRotateY(f.yaw)),
                          MatrixTranslate(f.pos.x, f.pos.y + p.hipY, f.pos.z));
}

// Shoulder / hip sockets in body space. Kept in one place so the renderer, the
// IK and the trail sampler all agree on where a limb actually is.
static Vector3 ShoulderL(Vector3 chest) { return VAdd(chest, { 0, -0.02f, -0.21f }); }
static Vector3 ShoulderR(Vector3 chest) { return VAdd(chest, { 0, -0.02f,  0.21f }); }
static const Vector3 HIP_L = { 0, -0.05f, -0.11f };
static const Vector3 HIP_R = { 0, -0.05f,  0.11f };

// World-space tip of a striking limb: where the swipe trail is sampled and
// where impact effects get planted.
static Vector3 LimbWorld(const Fighter &f, Limb limb) {
    const Pose &p = f.pose;
    Vector3 tdir = { sinf(p.lean), cosf(p.lean), 0 };
    Vector3 chest = VMul(tdir, 0.50f);
    Vector3 local;
    switch (limb) {
        case L_HAND: local = VAdd(ShoulderL(chest), p.lHand); break;
        case R_HAND: local = VAdd(ShoulderR(chest), p.rHand); break;
        case L_FOOT: local = VAdd(HIP_L, p.lFoot); break;
        case R_FOOT: local = VAdd(HIP_R, p.rFoot); break;
        default:     local = VAdd(chest, VMul(VAdd(p.lHand, p.rHand), 0.5f)); break;
    }
    return Vector3Transform(local, FighterRoot(f));
}

// Sample the striking limb once per simulation step while the move is swinging,
// and let the ribbon decay once it is over.
static void UpdateTrail(Fighter &f) {
    bool swinging = false;
    if (f.state == ST_ATTACK) {
        const MoveDef &mv = MOVES[f.move];
        swinging = f.moveFrame > mv.startup - 4 && f.moveFrame <= mv.startup + mv.active + 2;
    }
    if (swinging) {
        const MoveDef &mv = MOVES[f.move];
        Vector3 w = LimbWorld(f, mv.limb);
        if (f.trail.n < TRAIL_LEN) f.trail.p[f.trail.n++] = w;
        else {
            for (int i = 1; i < TRAIL_LEN; i++) f.trail.p[i - 1] = f.trail.p[i];
            f.trail.p[TRAIL_LEN - 1] = w;
        }
        f.trail.fade = 1.0f;
        bool foot = mv.limb == L_FOOT || mv.limb == R_FOOT;
        f.trail.c = f.raging ? Color{ 255, 90, 60, 255 }
                  : foot     ? Color{ 150, 200, 255, 255 }
                             : Color{ 255, 210, 150, 255 };
    } else {
        f.trail.fade *= 0.78f;
        if (f.trail.fade < 0.02f) f.trail.n = 0;
    }
}

static void DrawHead(const Matrix &parent, const Fighter &f, Vector3 head, Color skin, Color hair, Color accent) {
    DrawBall(parent, head, 0.125f, skin);
    DrawBlob(parent, VAdd(head, { 0.045f, -0.055f, 0 }), { 0.085f, 0.065f, 0.085f }, 0, skin);   // jaw
    DrawBlob(parent, VAdd(head, { 0.108f, 0.012f, 0 }), { 0.030f, 0.026f, 0.026f }, 0, skin);   // nose
    for (int s = -1; s <= 1; s += 2) {
        DrawBall(parent, VAdd(head, { 0.098f, 0.032f, 0.048f * s }), 0.020f, { 26, 22, 26, 255 });
        DrawBlob(parent, VAdd(head, { 0.086f, 0.068f, 0.052f * s }), { 0.028f, 0.016f, 0.038f }, 0, hair);  // brow
    }
    if (f.id == 0) {
        DrawBlob(parent, VAdd(head, { -0.030f, 0.042f, 0 }), { 0.118f, 0.110f, 0.122f }, 0, hair);
        DrawLimb(parent, VAdd(head, { 0, 0.018f, 0 }), VAdd(head, { 0, 0.052f, 0 }), 0.131f, accent);       // headband
        SurfCloth();
        DrawLimb(parent, VAdd(head, { -0.10f, 0.03f, 0.02f }), VAdd(head, { -0.26f, -0.05f, 0.06f }), 0.018f, accent);  // band tail
        SurfSkin();
    } else {
        // cropped light hair with a widow's peak, no band
        DrawBlob(parent, VAdd(head, { -0.018f, 0.052f, 0 }), { 0.116f, 0.098f, 0.124f }, 0, hair);
        DrawBlob(parent, VAdd(head, { 0.072f, 0.086f, 0 }), { 0.050f, 0.035f, 0.070f }, 0, hair);
    }
}

static void DrawFighter(const Fighter &f) {
    const Pose &p = f.pose;
    Matrix parent = FighterRoot(f);
    if (G.shadowPass) parent = MatrixMultiply(parent, G.shadowProj);

    Color white = { 255, 255, 255, 255 };
    float hit = f.flash * 0.7f;
    Color skin = Mix(f.skin, white, hit), cloth = Mix(f.cloth, white, hit);
    Color accent = Mix(f.accent, white, hit), hair = Mix(f.hair, white, f.flash * 0.5f);
    Color trunk = Mix(cloth, { 0, 0, 0, 255 }, 0.25f);

    // rage makes the accent kit glow and pulse
    float rage = f.raging ? 0.35f + 0.25f * sinf(f.breath * 3.0f) : 0.0f;

    Vector3 hip = { 0, 0, 0 };
    Vector3 tdir = { sinf(p.lean), cosf(p.lean), 0 };
    Vector3 waist = VMul(tdir, 0.16f);
    Vector3 chestLow = VMul(tdir, 0.34f);
    Vector3 chest = VMul(tdir, 0.50f);
    Vector3 neck = VAdd(chest, VMul(tdir, 0.16f));
    Vector3 head = VAdd(chest, VMul(tdir, 0.30f));
    float puff = 1.0f + 0.03f * sinf(f.breath);      // ribcage rise and fall

    // ---- torso ----
    SurfSkin();
    DrawTaper(parent, waist, chestLow, 0.150f, 0.172f * puff, skin);
    DrawTaper(parent, chestLow, chest, 0.172f * puff, 0.180f * puff, skin);
    DrawBlob(parent, chest, { 0.175f, 0.165f, 0.205f }, 0, skin);
    DrawBlob(parent, chestLow, { 0.130f, 0.115f, 0.175f }, 0, skin);                      // abs slab
    for (int s = -1; s <= 1; s += 2)                                                      // pecs
        DrawBlob(parent, VAdd(chest, { 0.070f, -0.035f, 0.088f * s }), { 0.072f, 0.070f, 0.098f }, 0, skin);
    DrawLimb(parent, chest, neck, 0.058f, skin);

    // ---- hips, trunks, belt ----
    SurfCloth();
    DrawBlob(parent, VAdd(hip, { 0, -0.03f, 0 }), { 0.165f, 0.180f, 0.195f }, 0, trunk);
    DrawBlob(parent, VAdd(hip, { -0.02f, -0.14f, 0 }), { 0.150f, 0.120f, 0.185f }, 0, trunk);
    SurfLeather();
    DrawLimb(parent, VAdd(hip, { 0, 0.03f, 0 }), VAdd(hip, VMul(tdir, 0.11f)), 0.178f, accent);
    Surf(0.10f, 12.0f, rage * 0.8f, 0.20f);
    DrawBlob(parent, VAdd(hip, { 0.15f, 0.06f, 0 }), { 0.050f, 0.050f, 0.038f }, 0, accent);   // buckle
    // sash tail, swings with the lean
    float sway = sinf(f.breath * 0.8f) * 0.05f + p.lean * 0.2f;
    SurfCloth();
    DrawTaper(parent, VAdd(hip, { -0.12f, 0.02f, 0.06f }), VAdd(hip, { -0.20f - sway, -0.34f, 0.12f }), 0.036f, 0.018f, accent);

    SurfSkin();
    DrawHead(parent, f, head, skin, hair, accent);

    // ---- arms ----
    for (int s = -1; s <= 1; s += 2) {
        Vector3 sh = VAdd(chest, { 0, -0.02f, 0.21f * s });
        Vector3 hand = VAdd(sh, s < 0 ? p.lHand : p.rHand);
        Vector3 elbow = SolveJoint(sh, hand, 0.30f, 0.30f, { -0.2f, -1.0f, 0.5f * s });
        SurfSkin();
        DrawBlob(parent, sh, { 0.092f, 0.088f, 0.092f }, 0, skin);                        // deltoid
        DrawTaper(parent, sh, elbow, 0.066f, 0.050f, skin);                               // biceps -> elbow
        DrawBall(parent, elbow, 0.052f, skin);
        DrawTaper(parent, elbow, hand, 0.055f, 0.042f, skin);                             // forearm
        // wrap + glove
        SurfCloth();
        Vector3 wrist = Vector3Lerp(hand, elbow, 0.22f);
        DrawLimb(parent, wrist, Vector3Lerp(hand, elbow, 0.05f), 0.047f, { 236, 232, 226, 255 });
        Surf(0.55f, 54.0f, rage, 0.26f);
        DrawBlob(parent, hand, { 0.082f, 0.076f, 0.070f }, 0, accent);
        Vector3 knuck = VAdd(hand, VMul(Vector3Normalize(Vector3Subtract(hand, elbow)), 0.045f));
        DrawBlob(parent, knuck, { 0.050f, 0.062f, 0.062f }, 0, Mix(accent, white, 0.15f));
    }

    // ---- legs ----
    for (int s = -1; s <= 1; s += 2) {
        Vector3 hj = { 0, -0.05f, 0.11f * s };
        Vector3 foot = VAdd(hj, s < 0 ? p.lFoot : p.rFoot);
        Vector3 knee = SolveJoint(hj, foot, 0.47f, 0.47f, { 1.0f, 0.1f, 0.25f * s });
        SurfCloth();
        DrawTaper(parent, hj, knee, 0.100f, 0.072f, cloth);                               // thigh
        DrawBall(parent, knee, 0.074f, cloth);
        DrawTaper(parent, knee, foot, 0.078f, 0.052f, cloth);                             // calf -> ankle
        SurfLeather();
        DrawBlob(parent, knee, { 0.078f, 0.070f, 0.072f }, 0, Mix(cloth, white, 0.18f));  // knee pad
        DrawBlob(parent, foot, { 0.070f, 0.062f, 0.068f }, 0, accent);                    // boot
        DrawBlob(parent, VAdd(foot, { 0.11f, -0.020f, 0 }), { 0.085f, 0.048f, 0.060f }, 0, accent);
    }
    SurfSkin();
}

// Ribbon of fading spheres following a limb through its arc.
static void DrawTrail(const Fighter &f) {
    const Trail &tr = f.trail;
    if (tr.n < 2 || tr.fade <= 0.01f) return;
    for (int i = 1; i < tr.n; i++) {
        float k = (float)i / (tr.n - 1);              // 0 oldest, 1 newest
        float a = k * k * tr.fade;
        float r = Lerp(0.035f, 0.115f, k);
        Color c = Fade(tr.c, a * 0.75f);
        DrawSphere(Vector3Lerp(tr.p[i - 1], tr.p[i], 0.5f), r, c);
        DrawSphere(tr.p[i], r * 0.85f, c);
    }
}

// ----------------------------------------------------------------------------
// Rendering: stage
// ----------------------------------------------------------------------------
static void DrawDisc(Vector3 c, float r, float h, Color col) {
    Matrix m = MatrixMultiply(MatrixScale(r, h, r), MatrixTranslate(c.x, c.y, c.z));
    G.mat.maps[MATERIAL_MAP_DIFFUSE].color = G.shadowPass ? SHADOW_COL : col;
    DrawMesh(G.disc, G.mat, m);
}

// Deterministic per-index noise so the crowd and the stars stay put between frames.
static float Hash(int i) {
    float x = sinf(i * 12.9898f + 78.233f) * 43758.5453f;
    return x - floorf(x);
}

static const int TORCH_COUNT = 12;
static Vector3 TorchPos(int i) {
    float a = i * 2 * PI / TORCH_COUNT + 0.26f;
    return { cosf(a) * 14.5f, 5.45f, sinf(a) * 14.5f };
}

// Silhouetted spectators in the stands: two capsules each, bobbing out of phase.
static void DrawCrowd(float t) {
    SurfCloth();
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
            Matrix idm = MatrixIdentity();
            DrawTaper(idm, { c.x, c.y, c.z }, { c.x, c.y + 0.62f, c.z }, 0.22f, 0.17f, body);
            DrawBall(idm, { c.x, c.y + 0.80f, c.z }, 0.17f, Mix(body, { 90, 74, 70, 255 }, 0.45f));
        }
    }
}

static void DrawStage(float t) {
    Matrix idm = MatrixIdentity();

    // ---- ground and platform ----
    SurfStone();
    DrawBox({ 0, -0.6f, 0 }, { 400, 0.8f, 400 }, 0, { 18, 15, 24, 255 });
    DrawDisc({ 0, -0.22f, 0 }, ARENA_R + 2.2f, 0.22f, { 44, 40, 52, 255 });
    DrawDisc({ 0, -0.20f, 0 }, ARENA_R + 1.6f, 0.20f, { 58, 54, 66, 255 });
    // the mat itself catches a broad highlight, so it reads as polished
    Surf(0.30f, 42.0f, 0.0f, 0.06f);
    DrawDisc({ 0, -0.19f, 0 }, ARENA_R + 0.2f, 0.195f, { 78, 74, 90, 255 });
    DrawDisc({ 0, -0.002f, 0 }, ARENA_R - 0.5f, 0.006f, { 70, 66, 82, 255 });
    DrawDisc({ 0, 0.001f, 0 }, 3.2f, 0.006f, { 88, 80, 104, 255 });          // centre emblem
    DrawDisc({ 0, 0.003f, 0 }, 1.1f, 0.006f, { 116, 96, 132, 255 });
    SurfStone();

    // ---- floor markings (unlit lines, so they glow into the bloom) ----
    for (int i = 1; i <= 3; i++) DrawCircle3D({ 0, 0.013f, 0 }, i * 3.0f, { 1, 0, 0 }, 90, { 122, 112, 146, 255 });
    for (int k = 0; k < 2; k++)
        DrawCircle3D({ 0, 0.013f, 0 }, ARENA_R - k * 0.06f, { 1, 0, 0 }, 90, { 255, 138, 52, 255 });
    for (int i = 0; i < 16; i++) {
        float a = i * 2 * PI / 16;
        DrawLine3D({ cosf(a) * 3, 0.013f, sinf(a) * 3 }, { cosf(a) * ARENA_R, 0.013f, sinf(a) * ARENA_R }, { 104, 96, 124, 255 });
    }

    // ---- pillars and torches ----
    for (int i = 0; i < TORCH_COUNT; i++) {
        float a = i * 2 * PI / TORCH_COUNT + 0.26f;
        Vector3 c = { cosf(a) * 14.5f, 0, sinf(a) * 14.5f };
        SurfStone();
        DrawBox({ c.x, 0.25f, c.z }, { 1.6f, 0.5f, 1.6f }, -a, { 78, 72, 82, 255 });      // plinth
        DrawBox({ c.x, 2.6f, c.z }, { 1.05f, 4.4f, 1.05f }, -a, { 96, 88, 98, 255 });     // shaft
        DrawBox({ c.x, 2.6f, c.z }, { 1.16f, 0.9f, 1.16f }, -a, { 86, 79, 90, 255 });     // banding
        DrawBox({ c.x, 4.95f, c.z }, { 1.5f, 0.32f, 1.5f }, -a, { 122, 110, 114, 255 });  // capital
        SurfMetal();
        DrawBox({ c.x, 5.18f, c.z }, { 0.52f, 0.30f, 0.52f }, -a, { 84, 70, 54, 255 });   // brazier

        float flick = 0.30f + sinf(t * 9 + i * 2.1f) * 0.045f + sinf(t * 21.3f + i) * 0.02f;
        SurfGlow(2.2f);
        DrawBall(idm, { c.x, 5.45f, c.z }, flick, { 255, 150, 40, 255 });
        SurfGlow(4.0f);
        DrawBall(idm, { c.x, 5.53f, c.z }, flick * 0.58f, { 255, 232, 150, 255 });
        SurfStone();

        // hanging banner between this pillar and the next
        float a2 = (i + 1) * 2 * PI / TORCH_COUNT + 0.26f;
        Vector3 c2 = { cosf(a2) * 14.5f, 0, sinf(a2) * 14.5f };
        Vector3 mid = { (c.x + c2.x) * 0.5f, 4.15f, (c.z + c2.z) * 0.5f };
        float dip = 0.22f + sinf(t * 1.1f + i) * 0.05f;
        SurfCloth();
        DrawBox({ mid.x * 0.985f, mid.y - dip, mid.z * 0.985f }, { 5.2f, 1.5f, 0.10f },
                -(a + a2) * 0.5f + PI * 0.5f, i % 2 ? Color{ 92, 34, 46, 255 } : Color{ 40, 44, 84, 255 });
    }

    // ---- stands ----
    SurfStone();
    for (int i = 0; i < 18; i++) {
        float a = i * 2 * PI / 18;
        DrawBox({ cosf(a) * 19.5f, 0.9f, sinf(a) * 19.5f }, { 6.2f, 1.8f, 2.5f }, -a + PI * 0.5f, { 44, 40, 54, 255 });
        DrawBox({ cosf(a) * 22.5f, 1.9f, sinf(a) * 22.5f }, { 7.2f, 3.8f, 2.5f }, -a + PI * 0.5f, { 36, 33, 46, 255 });
        DrawBox({ cosf(a) * 25.4f, 3.1f, sinf(a) * 25.4f }, { 8.0f, 6.2f, 2.5f }, -a + PI * 0.5f, { 30, 27, 40, 255 });
    }
    DrawCrowd(t);

    // ---- far silhouettes ----
    SurfStone();
    for (int i = 0; i < 14; i++) {
        float a = i * 2 * PI / 14 + 0.1f;
        float hgt = 14 + 9 * sinf(i * 1.9f) * sinf(i * 0.7f + 1) + 6;
        Matrix m = MatrixMultiply(MatrixScale(17 + 4 * sinf(i * 3.1f), hgt, 17 + 4 * sinf(i * 3.1f)),
                                  MatrixTranslate(cosf(a) * 70, -1, sinf(a) * 70));
        G.mat.maps[MATERIAL_MAP_DIFFUSE].color = { 42, 31, 58, 255 };
        DrawMesh(G.cone, G.mat, m);
    }

    // ---- moon ----
    SurfGlow(3.0f);
    DrawBall(idm, { 45, 34, -80 }, 5.5f, { 255, 240, 214, 255 });
    SurfStone();
}

// ----------------------------------------------------------------------------
// Rendering: HUD
// ----------------------------------------------------------------------------
static void TextC(const char *s, int cx, int y, int size, Color c) {
    int w = MeasureText(s, size);
    DrawText(s, cx - w / 2 + 3, y + 3, size, Fade(BLACK, 0.7f * c.a / 255.0f));
    DrawText(s, cx - w / 2, y, size, c);
}

static void DrawHealthBar(const Fighter &f, bool left) {
    const int barW = 480, barH = 26, y = 42;
    int x = left ? 80 : SCREEN_W - 80 - barW;
    DrawRectangle(x - 4, y - 4, barW + 8, barH + 8, { 12, 12, 18, 230 });
    DrawRectangle(x, y, barW, barH, { 60, 20, 24, 255 });
    int wl = (int)(barW * f.hpLag / MAX_HP), w = barW * f.hp / MAX_HP;
    Color c1 = f.hp > MAX_HP * 0.3f ? Color{ 250, 210, 60, 255 } : Color{ 250, 90, 50, 255 };
    Color c2 = f.hp > MAX_HP * 0.3f ? Color{ 120, 220, 90, 255 } : Color{ 250, 170, 50, 255 };
    if (left) {                                         // bars drain toward the outside edge
        DrawRectangle(x + barW - wl, y, wl, barH, { 220, 50, 50, 255 });
        DrawRectangleGradientH(x + barW - w, y, w, barH, c2, c1);
    } else {
        DrawRectangle(x, y, wl, barH, { 220, 50, 50, 255 });
        DrawRectangleGradientH(x, y, w, barH, c1, c2);
    }
    DrawRectangleLinesEx({ (float)x - 4, (float)y - 4, (float)barW + 8, (float)barH + 8 }, 2, { 210, 210, 225, 255 });
    int nw = MeasureText(f.name, 24);
    DrawText(f.name, left ? x : x + barW - nw, y + barH + 10, 24, RAYWHITE);
    for (int i = 0; i < ROUNDS_TO_WIN; i++) {
        int cx = left ? x + barW - 14 - i * 30 : x + 14 + i * 30;
        DrawCircle(cx, y + barH + 22, 10, { 12, 12, 18, 230 });
        if (i < f.wins) DrawCircle(cx, y + barH + 22, 7, { 255, 190, 40, 255 });
        DrawCircleLines(cx, y + barH + 22, 10, { 210, 210, 225, 255 });
    }
    int sx = left ? 90 : SCREEN_W - 90;
    if (f.showT > 0 && f.showCount >= 2) {
        float a = fminf(1, f.showT / 20.0f);
        const char *s1 = TextFormat("%d HITS", f.showCount), *s2 = TextFormat("%d damage", f.showDmg);
        DrawText(s1, left ? sx : sx - MeasureText(s1, 44), 190, 44, Fade({ 255, 200, 60, 255 }, a));
        DrawText(s2, left ? sx : sx - MeasureText(s2, 22), 238, 22, Fade(RAYWHITE, a));
    }
    if (f.msgT > 0) {
        float a = fminf(1, f.msgT / 15.0f);
        DrawText(f.msg, left ? sx : sx - MeasureText(f.msg, 30), 150, 30, Fade({ 255, 110, 70, 255 }, a));
    }
}

static void DrawMoveList(int x, int y) {
    DrawText("MOVE LIST", x, y, 24, { 255, 190, 40, 255 });
    for (int i = 1; i < M_COUNT; i++) {
        const MoveDef &m = MOVES[i];
        const char *lv = m.level == LV_HIGH ? "high" : m.level == LV_MID ? "mid" : m.level == LV_LOW ? "low" : "throw";
        DrawText(m.input, x, y + 10 + i * 24, 20, { 140, 210, 255, 255 });
        DrawText(m.name, x + 90, y + 10 + i * 24, 20, RAYWHITE);
        DrawText(lv, x + 290, y + 10 + i * 24, 20, { 180, 180, 190, 255 });
    }
    int yy = y + 10 + M_COUNT * 24 + 6;
    DrawText("LP, RP        one-two string (add LK for a third hit)", x, yy, 18, { 200, 200, 210, 255 });
    DrawText("d+RP launches: follow with LP, RP, LK to juggle", x, yy + 22, 18, { 200, 200, 210, 255 });
    DrawText("Hold BACK to guard high/mid, DOWN+BACK to guard low", x, yy + 44, 18, { 200, 200, 210, 255 });
    DrawText("Crouch under highs and throws. Sidestep linear attacks.", x, yy + 66, 18, { 200, 200, 210, 255 });
}

static void DrawControls(int x, int y) {
    DrawText("PLAYER 1", x, y, 22, { 255, 120, 110, 255 });
    DrawText("A / D  walk (double tap = dash)\nW  hop     S  crouch\nQ / E  sidestep\nU = LP   I = RP\nJ = LK   K = RK\n(gamepad 1 also works)", x, y + 28, 18, RAYWHITE);
    DrawText("PLAYER 2", x + 330, y, 22, { 120, 170, 255, 255 });
    DrawText("LEFT / RIGHT  walk\nUP  hop     DOWN  crouch\n, / .  or  NUM 7 / 9  sidestep\nNUM 4 = LP   NUM 5 = RP\nNUM 1 = LK   NUM 2 = RK", x + 330, y + 28, 18, RAYWHITE);
}

static void DrawHUD() {
    static const char *modeName[3] = { "1P vs CPU", "2P VERSUS", "CPU vs CPU" };
    static const char *diffName[3] = { "EASY", "NORMAL", "HARD" };
    if (G.phase == PH_TITLE) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.45f));
        TextC("STEEL KNUCKLE", SCREEN_W / 2, 70, 96, { 255, 190, 40, 255 });
        TextC("3D arena fighter", SCREEN_W / 2, 168, 26, RAYWHITE);
        TextC(TextFormat("F1  mode: %s        F2  difficulty: %s", modeName[G.mode], diffName[G.difficulty]), SCREEN_W / 2, 225, 24, { 140, 210, 255, 255 });
        if (((int)(GetTime() * 2)) % 2 == 0) TextC("PRESS ENTER", SCREEN_W / 2, 270, 34, RAYWHITE);
        DrawControls(90, 350);
        DrawMoveList(790, 330);
        return;
    }
    DrawHealthBar(G.f[0], true);
    DrawHealthBar(G.f[1], false);
    int secs = (G.timer + 59) / 60;
    TextC(TextFormat("%02d", secs), SCREEN_W / 2, 30, 56, secs <= 10 ? Color{ 255, 90, 70, 255 } : RAYWHITE);

    if (G.bannerT > 0) {
        float a = fminf(1, G.bannerT / 12.0f);
        int size = 110;
        TextC(G.banner, SCREEN_W / 2, SCREEN_H / 2 - 90, size, Fade({ 255, 200, 50, 255 }, a));
    }
    if (G.phase == PH_MATCH_END) {
        int w = G.f[0].wins >= ROUNDS_TO_WIN ? 0 : 1;
        DrawRectangle(0, SCREEN_H / 2 - 120, SCREEN_W, 230, Fade(BLACK, 0.6f));
        TextC(TextFormat("%s WINS THE MATCH", G.f[w].name), SCREEN_W / 2, SCREEN_H / 2 - 95, 64, { 255, 200, 50, 255 });
        TextC("ENTER  rematch        BACKSPACE  title", SCREEN_W / 2, SCREEN_H / 2 + 20, 28, RAYWHITE);
    }
    if (G.paused) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.7f));
        TextC("PAUSED", SCREEN_W / 2, 60, 64, RAYWHITE);
        DrawControls(90, 200);
        DrawMoveList(790, 180);
        TextC("P  resume        BACKSPACE  title", SCREEN_W / 2, SCREEN_H - 70, 24, { 200, 200, 210, 255 });
    } else if (G.phase == PH_FIGHT) {
        DrawText("P pause / move list", 20, SCREEN_H - 28, 18, Fade(RAYWHITE, 0.5f));
    }
}

static const int SS = 2;               // supersampling factor for the scene target
static const Vector3 LIGHT_DIR = { -0.4499f, -0.7998f, -0.3499f };

// Blit a render texture, flipping it back the right way up.
static void DrawRT(const RenderTexture2D &rt, int w, int h) {
    DrawTexturePro(rt.texture, { 0, 0, (float)rt.texture.width, -(float)rt.texture.height },
                   { 0, 0, (float)w, (float)h }, { 0, 0 }, 0, WHITE);
}

static void DrawSky(float t, int w, int h) {
    DrawRectangleGradientV(0, 0,            w, h * 52 / 100, { 12, 10, 30, 255 }, { 74, 34, 70, 255 });
    DrawRectangleGradientV(0, h * 52 / 100, w, h * 15 / 100, { 74, 34, 70, 255 }, { 206, 100, 68, 255 });
    DrawRectangleGradientV(0, h * 67 / 100, w, h * 33 / 100, { 206, 100, 68, 255 }, { 34, 23, 42, 255 });
    for (int i = 0; i < 170; i++) {
        float sx = Hash(i * 3 + 1) * w, sy = Hash(i * 3 + 2) * h * 0.52f;
        float tw = 0.35f + 0.65f * fabsf(sinf(t * (0.5f + Hash(i * 3 + 3)) + i));
        float dim = 1.0f - sy / (h * 0.55f);
        DrawCircleV({ sx, sy }, Hash(i * 7) * 1.5f + 0.5f, Fade(RAYWHITE, tw * 0.75f * dim));
    }
}

static Camera3D BuildCamera() {
    Camera3D cam{};
    Vector3 sh = { FrandS() * G.shake, FrandS() * G.shake * 0.7f, FrandS() * G.shake };
    cam.target = VAdd(G.camTarget, sh);
    cam.position = VAdd(cam.target, { cosf(G.camAngle) * G.camDist, 0.55f + G.camDist * 0.045f, sinf(G.camAngle) * G.camDist });
    cam.projection = CAMERA_PERSPECTIVE;
    cam.fovy = 40.0f - G.camPunch * 7.0f;                 // punch in on impact
    Vector3 fw = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 rt = Vector3Normalize(Vector3CrossProduct(fw, { 0, 1, 0 }));
    Vector3 up = Vector3CrossProduct(rt, fw);
    cam.up = Vector3Normalize(VAdd(up, VMul(rt, G.camRoll)));
    return cam;
}

static void UploadLights(const Camera3D &cam) {
    Vector3 light = LIGHT_DIR;
    Vector3 keyCol = { 1.02f, 0.86f, 0.70f };
    Vector3 skyCol = { 0.20f, 0.20f, 0.34f };
    Vector3 gndCol = { 0.11f, 0.08f, 0.13f };
    Vector3 rimCol = { 1.00f, 0.60f, 0.34f };
    Vector3 fogCol = { 0.19f, 0.13f, 0.24f };
    float fogAmt = 0.011f;
    SetShaderValue(G.lit, G.locLight, &light, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locView, &cam.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locKey, &keyCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locSky, &skyCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locGnd, &gndCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locRim, &rimCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locFogCol, &fogCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locFogAmt, &fogAmt, SHADER_UNIFORM_FLOAT);

    // Only the four braziers nearest the action contribute; the rest are too far
    // to matter and the shader loop is fixed size.
    Vector3 mid = { (G.f[0].pos.x + G.f[1].pos.x) * 0.5f, 0, (G.f[0].pos.z + G.f[1].pos.z) * 0.5f };
    int idx[MAX_PT_LIGHTS] = { 0, 0, 0, 0 };
    float best[MAX_PT_LIGHTS] = { 1e9f, 1e9f, 1e9f, 1e9f };
    for (int i = 0; i < TORCH_COUNT; i++) {
        Vector3 tp = TorchPos(i);
        float d = (tp.x - mid.x) * (tp.x - mid.x) + (tp.z - mid.z) * (tp.z - mid.z);
        for (int s = 0; s < MAX_PT_LIGHTS; s++) {
            if (d < best[s]) {
                for (int k = MAX_PT_LIGHTS - 1; k > s; k--) { best[k] = best[k - 1]; idx[k] = idx[k - 1]; }
                best[s] = d; idx[s] = i;
                break;
            }
        }
    }
    float t = (float)GetTime();
    Vector3 pos[MAX_PT_LIGHTS], col[MAX_PT_LIGHTS];
    for (int s = 0; s < MAX_PT_LIGHTS; s++) {
        pos[s] = TorchPos(idx[s]);
        float fl = 0.55f + 0.14f * sinf(t * 9 + idx[s] * 2.1f) + 0.06f * sinf(t * 23.7f + idx[s]);
        col[s] = { 1.05f * fl, 0.46f * fl, 0.14f * fl };
    }
    SetShaderValueV(G.lit, G.locPtPos, pos, SHADER_UNIFORM_VEC3, MAX_PT_LIGHTS);
    SetShaderValueV(G.lit, G.locPtCol, col, SHADER_UNIFORM_VEC3, MAX_PT_LIGHTS);
}

static void DrawWorld(float t, const Camera3D &cam) {
    BeginMode3D(cam);
    DrawStage(t);

    // soft contact patch under each fighter
    for (auto &f : G.f) {
        float r = fmaxf(0.22f, 0.52f - f.pos.y * 0.13f);
        float lie = (f.state == ST_DOWN || f.state == ST_KO) ? 0.35f : 0;
        float a = Clamp(0.42f - f.pos.y * 0.10f, 0.08f, 0.42f);
        Vector3 c = { f.pos.x - cosf(f.yaw) * lie, 0.015f, f.pos.z + sinf(f.yaw) * lie };
        DrawCylinder(c, r + lie, r + lie, 0.004f, 24, Fade(BLACK, a));
        DrawCylinder(c, (r + lie) * 0.55f, (r + lie) * 0.55f, 0.005f, 20, Fade(BLACK, a * 0.55f));
    }

    // planar shadows: the body flattened onto the mat. Depth writes at a single
    // plane mean overlapping limbs can't double-darken.
    rlDisableBackfaceCulling();
    G.mat.shader = G.flat;
    G.shadowPass = true;
    for (auto &f : G.f) DrawFighter(f);
    G.shadowPass = false;
    G.mat.shader = G.lit;
    rlEnableBackfaceCulling();

    DrawFighter(G.f[0]);
    DrawFighter(G.f[1]);
    for (auto &f : G.f) DrawTrail(f);

    for (auto &p : G.parts) {
        float a = Clamp(p.life, 0, 1);
        DrawCube(p.p, p.size, p.size, p.size, Fade(p.c, a));
    }
    for (auto &f : G.flashes) {
        DrawSphere(f.p, 0.12f + f.t * 0.50f, Fade(f.c, (1 - f.t) * 0.55f));
        DrawSphere(f.p, 0.05f + f.t * 0.22f, Fade(WHITE, (1 - f.t) * 0.60f));
        // impact ring, expanding flat against the camera-ish plane
        DrawCircle3D(f.p, 0.10f + f.t * 1.05f, { 1, 0, 0 }, 90, Fade(f.c, (1 - f.t) * 0.45f));
        DrawCircle3D(f.p, 0.10f + f.t * 1.05f, { 0, 0, 1 }, 90, Fade(f.c, (1 - f.t) * 0.30f));
    }
    EndMode3D();
}

static void DrawFrame() {
    float t = (float)GetTime();
    Camera3D cam = BuildCamera();
    UploadLights(cam);

    // shadow projection onto y = 0.028, along the key light
    float k = -LIGHT_DIR.y;
    Matrix sp = MatrixIdentity();
    sp.m4 = LIGHT_DIR.x / k; sp.m5 = 0; sp.m6 = LIGHT_DIR.z / k;
    sp.m13 = 0.028f;
    G.shadowProj = sp;

    if (!G.postFx) {
        BeginDrawing();
        ClearBackground({ 20, 16, 34, 255 });
        DrawSky(t, SCREEN_W, SCREEN_H);
        DrawWorld(t, cam);
        DrawHUD();
        EndDrawing();
        return;
    }

    // ---- scene, supersampled ----
    BeginTextureMode(G.scene);
        ClearBackground({ 20, 16, 34, 255 });
        DrawSky(t, SCREEN_W * SS, SCREEN_H * SS);
        DrawWorld(t, cam);
    EndTextureMode();

    // ---- bloom: threshold, then two separable blur passes at quarter res ----
    int bw = G.bloomA.texture.width, bh = G.bloomA.texture.height;
    BeginTextureMode(G.bloomA);
        ClearBackground(BLANK);
        BeginShaderMode(G.bright);
            DrawRT(G.scene, bw, bh);
        EndShaderMode();
    EndTextureMode();

    for (int pass = 0; pass < 2; pass++) {
        Vector2 dir = { 1.0f / bw * (1.0f + pass), 0 };
        BeginTextureMode(G.bloomB);
            ClearBackground(BLANK);
            BeginShaderMode(G.blur);
                SetShaderValue(G.blur, G.locBlurDir, &dir, SHADER_UNIFORM_VEC2);
                DrawRT(G.bloomA, bw, bh);
            EndShaderMode();
        EndTextureMode();
        dir = { 0, 1.0f / bh * (1.0f + pass) };
        BeginTextureMode(G.bloomA);
            ClearBackground(BLANK);
            BeginShaderMode(G.blur);
                SetShaderValue(G.blur, G.locBlurDir, &dir, SHADER_UNIFORM_VEC2);
                DrawRT(G.bloomB, bw, bh);
            EndShaderMode();
        EndTextureMode();
    }

    // ---- composite ----
    BeginDrawing();
        ClearBackground(BLACK);
        float bloomAmt = 0.85f, vig = 0.55f, aberr = 0.55f, grain = 0.045f;
        BeginShaderMode(G.composite);
            SetShaderValueTexture(G.composite, G.locBloomTex, G.bloomA.texture);
            SetShaderValue(G.composite, G.locBloomAmt, &bloomAmt, SHADER_UNIFORM_FLOAT);
            SetShaderValue(G.composite, G.locVig, &vig, SHADER_UNIFORM_FLOAT);
            SetShaderValue(G.composite, G.locAberr, &aberr, SHADER_UNIFORM_FLOAT);
            SetShaderValue(G.composite, G.locGrain, &grain, SHADER_UNIFORM_FLOAT);
            SetShaderValue(G.composite, G.locTime, &t, SHADER_UNIFORM_FLOAT);
            SetShaderValue(G.composite, G.locFlashAmt, &G.screenFlash, SHADER_UNIFORM_FLOAT);
            SetShaderValue(G.composite, G.locFlashCol, &G.flashCol, SHADER_UNIFORM_VEC3);
            DrawRT(G.scene, SCREEN_W, SCREEN_H);
        EndShaderMode();
        DrawHUD();
    EndDrawing();
}

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------
int main(int argc, char **argv) {
    int shotFrame = -1;
    bool demo = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--demo")) demo = true;
        if (!strcmp(argv[i], "--shot") && i + 1 < argc) shotFrame = atoi(argv[++i]);
    }

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Steel Knuckle");
    SetTargetFPS(60);
    InitAudioDevice();
    G.audio = IsAudioDeviceReady();
    if (G.audio) for (int i = 0; i < SND_COUNT; i++) G.snd[i] = MakeSound((SoundId)i);

    G.sphere = GenMeshSphere(1.0f, 14, 20);
    G.cyl = GenMeshCylinder(1.0f, 1.0f, 14);
    G.cube = GenMeshCube(1, 1, 1);
    G.disc = GenMeshCylinder(1.0f, 1.0f, 64);
    G.cone = GenMeshCone(1.0f, 1.0f, 7);

    G.mat = LoadMaterialDefault();
    G.flat = G.mat.shader;                       // raylib's unlit default, kept for shadows
    G.lit = LoadShaderFromMemory(VS_SRC, FS_SRC);
    G.mat.shader = G.lit;
    G.locLight   = GetShaderLocation(G.lit, "lightDir");
    G.locView    = GetShaderLocation(G.lit, "viewPos");
    G.locKey     = GetShaderLocation(G.lit, "keyCol");
    G.locSky     = GetShaderLocation(G.lit, "skyCol");
    G.locGnd     = GetShaderLocation(G.lit, "gndCol");
    G.locRim     = GetShaderLocation(G.lit, "rimCol");
    G.locFogCol  = GetShaderLocation(G.lit, "fogCol");
    G.locPtPos   = GetShaderLocation(G.lit, "ptPos");
    G.locPtCol   = GetShaderLocation(G.lit, "ptCol");
    G.locSurf    = GetShaderLocation(G.lit, "surf");
    G.locGloss   = GetShaderLocation(G.lit, "gloss");
    G.locEmis    = GetShaderLocation(G.lit, "emis");
    G.locRimAmt  = GetShaderLocation(G.lit, "rimAmt");
    G.locFogAmt  = GetShaderLocation(G.lit, "fogAmt");

    G.bright = LoadShaderFromMemory(0, FS_BRIGHT);
    G.blur = LoadShaderFromMemory(0, FS_BLUR);
    G.locBlurDir = GetShaderLocation(G.blur, "dir");
    G.composite = LoadShaderFromMemory(0, FS_COMPOSITE);
    G.locBloomTex = GetShaderLocation(G.composite, "bloomTex");
    G.locBloomAmt = GetShaderLocation(G.composite, "bloomAmt");
    G.locVig      = GetShaderLocation(G.composite, "vignette");
    G.locAberr    = GetShaderLocation(G.composite, "aberr");
    G.locGrain    = GetShaderLocation(G.composite, "grain");
    G.locTime     = GetShaderLocation(G.composite, "time");
    G.locFlashAmt = GetShaderLocation(G.composite, "flashAmt");
    G.locFlashCol = GetShaderLocation(G.composite, "flashCol");

    G.scene  = LoadRenderTexture(SCREEN_W * SS, SCREEN_H * SS);
    G.bloomA = LoadRenderTexture(SCREEN_W / 2, SCREEN_H / 2);
    G.bloomB = LoadRenderTexture(SCREEN_W / 2, SCREEN_H / 2);
    SetTextureFilter(G.scene.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(G.bloomA.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(G.bloomB.texture, TEXTURE_FILTER_BILINEAR);
    Surf(0.2f, 24.0f, 0.0f, 0.3f);

    ResetFighter(G.f[0], 0); ResetFighter(G.f[1], 1);
    if (demo) { G.mode = 2; StartMatch(); }

    float acc = 0;
    while (!WindowShouldClose()) {
        G.frame++;
        // menu keys
        if (G.phase == PH_TITLE) {
            if (IsKeyPressed(KEY_F1)) G.mode = (G.mode + 1) % 3;
            if (IsKeyPressed(KEY_F2)) G.difficulty = (G.difficulty + 1) % 3;
            if (IsKeyPressed(KEY_ENTER) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) StartMatch();
        } else {
            if (IsKeyPressed(KEY_P) && G.phase == PH_FIGHT) G.paused = !G.paused;
            if (IsKeyPressed(KEY_BACKSPACE) && (G.paused || G.phase == PH_MATCH_END)) {
                G.paused = false; G.phase = PH_TITLE; G.bannerT = 0;
                G.f[0].wins = G.f[1].wins = 0; ResetFighter(G.f[0], 0); ResetFighter(G.f[1], 1);
            }
            if (IsKeyPressed(KEY_ENTER) && G.phase == PH_MATCH_END) StartMatch();
        }

        Input h0 = ReadHuman(0), h1 = ReadHuman(1);
        acc = fminf(acc + GetFrameTime(), DT * 4);
        bool first = true;
        while (acc >= DT) {
            Input in0 = h0, in1 = h1;
            bool thinking = G.phase == PH_FIGHT && !G.paused && G.hitstop == 0 && (G.slowmo == 0);
            if (G.mode == 2) in0 = thinking ? ThinkAI(G.ai[0], G.f[0], G.f[1], G.difficulty) : Input();
            if (G.mode != 1) in1 = thinking ? ThinkAI(G.ai[1], G.f[1], G.f[0], G.difficulty) : Input();
            bool aiFirst0 = first || G.mode == 2, aiFirst1 = first || G.mode != 1;
            if (!aiFirst0) ClearPressed(in0);
            if (!aiFirst1) ClearPressed(in1);
            Step(true, in0, in1);
            first = false;
            acc -= DT;
        }

        DrawFrame();
        if (shotFrame >= 0 && G.frame == shotFrame) { TakeScreenshot("shot.png"); break; }
    }

    UnloadShader(G.lit); UnloadShader(G.bright); UnloadShader(G.blur); UnloadShader(G.composite);
    UnloadRenderTexture(G.scene); UnloadRenderTexture(G.bloomA); UnloadRenderTexture(G.bloomB);
    UnloadMesh(G.sphere); UnloadMesh(G.cyl); UnloadMesh(G.cube); UnloadMesh(G.disc); UnloadMesh(G.cone);
    if (G.audio) { for (int i = 0; i < SND_COUNT; i++) UnloadSound(G.snd[i]); }
    CloseAudioDevice();
    CloseWindow();
    return 0;
}