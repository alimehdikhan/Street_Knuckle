// =============================================================================
//  game.cpp - the fight itself.
// =============================================================================
#include "game.h"
#include "audio.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

Game G;

// -----------------------------------------------------------------------------
// Move table
//
//  startup/active/recovery are frames at 60Hz. `range` is reach along the
//  attacker's forward axis; `lunge` is how far the move travels during startup.
//  windup/strike are the limb target in body space, so the animation is a
//  property of the move rather than hand-keyed per state.
// -----------------------------------------------------------------------------
const MoveDef MOVES[M_COUNT] = {
    { "", "", 0,0,0, LV_HIGH, 0, 0, 0,0, 0,0, FX_NORMAL, 0, false, L_HAND, {0,0,0}, {0,0,0}, 0,0, 0.92f,0.92f, false,false },

    { "Jab",             "LP",     10,3,13, LV_HIGH,  6, 1.25f, 21,17, 0.050f,0.040f, FX_NORMAL,    0.12f, false, L_HAND,
      { 0.10f,-0.05f, 0.05f}, {0.60f, 0.04f, 0.12f},  0.05f, 0.22f, 0.92f,0.92f, false,false },
    { "Straight",        "RP",     12,3,17, LV_HIGH,  9, 1.35f, 23,17, 0.070f,0.050f, FX_NORMAL,    0.15f, false, R_HAND,
      {-0.05f,-0.08f,-0.02f}, {0.62f, 0.03f,-0.14f},  0.00f, 0.32f, 0.92f,0.92f, false,false },
    { "Mid Kick",        "LK",     14,3,21, LV_MID,  13, 1.60f, 26,16, 0.080f,0.060f, FX_NORMAL,    0.15f, false, L_FOOT,
      { 0.25f,-0.40f, 0.00f}, {0.88f,-0.12f, 0.05f}, -0.05f,-0.30f, 0.94f,0.94f, false,false },
    { "Roundhouse",      "RK",     16,4,25, LV_HIGH, 18, 1.65f,  0,18, 0.000f,0.070f, FX_KNOCKDOWN, 0.20f, false, R_FOOT,
      { 0.10f,-0.35f, 0.20f}, {0.76f, 0.52f,-0.05f}, -0.10f,-0.45f, 0.95f,0.96f, false,false },

    { "Crouch Jab",      "d+LP",   10,3,14, LV_MID,   5, 1.20f, 20,14, 0.050f,0.040f, FX_NORMAL,    0.08f, true,  L_HAND,
      { 0.10f,-0.10f, 0.05f}, {0.60f,-0.05f, 0.12f},  0.30f, 0.45f, 0.58f,0.58f, false,false },
    { "Rising Uppercut", "d+RP",   15,3,26, LV_MID,  14, 1.30f,  0,18, 0.000f,0.050f, FX_LAUNCH,    0.20f, false, R_HAND,
      { 0.15f,-0.45f,-0.05f}, {0.42f, 0.42f,-0.12f},  0.50f,-0.12f, 0.65f,0.98f, false,false },
    { "Low Kick",        "d+LK",   16,3,23, LV_LOW,   8, 1.55f, 23,12, 0.040f,0.030f, FX_NORMAL,    0.10f, true,  L_FOOT,
      { 0.20f,-0.35f, 0.00f}, {0.88f,-0.48f, 0.05f},  0.20f, 0.10f, 0.58f,0.58f, false,false },
    { "Sweep",           "d+RK",   22,4,33, LV_LOW,  15, 1.75f,  0,12, 0.000f,0.030f, FX_SWEEP,     0.20f, true,  R_FOOT,
      {-0.10f,-0.40f, 0.30f}, {0.92f,-0.42f,-0.05f},  0.30f, 0.15f, 0.52f,0.50f, false,false },

    { "Power Straight",  "f+RP",   18,3,27, LV_MID,  20, 1.60f,  0,22, 0.000f,0.100f, FX_WALLSPLAT, 0.55f, false, R_HAND,
      {-0.15f,-0.10f,-0.05f}, {0.64f,-0.10f,-0.14f}, -0.10f, 0.50f, 0.90f,0.85f, true, false },
    { "Advance Kick",    "f+LK",   15,3,19, LV_MID,  11, 1.70f, 24,14, 0.090f,0.080f, FX_NORMAL,    0.40f, false, L_FOOT,
      { 0.22f,-0.42f, 0.02f}, {0.94f,-0.20f, 0.04f}, -0.02f,-0.24f, 0.93f,0.92f, false,false },
    { "Backfist",        "b+RP",   20,4,24, LV_HIGH, 16, 1.55f,  0,16, 0.000f,0.060f, FX_KNOCKDOWN, 0.10f, false, R_HAND,
      { 0.05f, 0.10f, 0.30f}, {0.66f, 0.10f,-0.22f},  0.18f, 0.10f, 0.92f,0.92f, true, false },
    { "Hopkick",         "u+RK",   17,4,30, LV_MID,  16, 1.50f,  0,20, 0.000f,0.070f, FX_LAUNCH,    0.22f, false, R_FOOT,
      { 0.05f,-0.45f, 0.10f}, {0.70f, 0.30f,-0.04f}, -0.20f,-0.40f, 1.02f,1.10f, false,false },
    { "Rising Knee",     "WR+RP",  14,3,24, LV_MID,  15, 1.35f,  0,17, 0.000f,0.050f, FX_LAUNCH,    0.25f, false, R_HAND,
      { 0.12f,-0.40f,-0.05f}, {0.50f, 0.36f,-0.10f},  0.40f,-0.05f, 0.62f,0.98f, false,false },

    { "Shoulder Throw",  "LP+LK",  12,2,27, LV_THROW,28, 1.15f,  0, 0, 0.000f,0.000f, FX_NORMAL,    0.20f, false, BOTH_HANDS,
      { 0.15f,-0.10f, 0.00f}, {0.50f, 0.00f, 0.00f},  0.10f, 0.30f, 0.92f,0.90f, false,false },
    { "Rage Art",        "LP+RP",  16,5,40, LV_MID,  45, 1.70f,  0,24, 0.000f,0.120f, FX_LAUNCH,    0.60f, false, BOTH_HANDS,
      {-0.20f,-0.20f, 0.00f}, {0.72f, 0.10f, 0.00f}, -0.25f, 0.55f, 0.88f,0.84f, true, true },
    { "Flying Kick", "AIR+LK", 8,6,20, LV_MID, 20, 1.90f, 0,18, 0,0.08f, FX_KNOCKDOWN, 0.35f, false, L_FOOT,
      {0.15f,-0.30f,0}, {0.96f,-0.20f,0.04f}, -0.15f,-0.42f, 0.88f,0.88f, false,false },
    { "Palm Wave", "236+P", 14,1,32, LV_SPECIAL,12,1.3f,28,20,0.055f,0.04f,FX_NORMAL,0,false,BOTH_HANDS,
      {-0.18f,-0.12f,0}, {0.70f,-0.08f,0}, -0.12f,0.35f,0.88f,0.86f,false,false },
    { "Rising Fang", "623+P", 7,7,32, LV_SPECIAL,20,1.35f,0,16,0,0.08f,FX_LAUNCH,0.28f,false,R_HAND,
      {0.08f,-0.48f,-0.1f}, {0.55f,0.60f,-0.1f},0.45f,-0.18f,0.60f,1.04f,false,false },
    { "Cyclone Kick", "214+K", 12,5,25, LV_HIGH,17,1.7f,0,18,0,0.075f,FX_KNOCKDOWN,0.75f,false,R_FOOT,
      {-0.08f,-0.35f,0.30f}, {0.88f,0.24f,-0.15f},0.10f,-0.30f,0.91f,0.96f,true,false },
    { "Tornado Heel", "b+LK", 18,3,24, LV_MID,12,1.75f,28,15,0.06f,0.055f,FX_TORNADO,0.20f,false,L_FOOT,
      {0.16f,-0.45f,0.24f}, {0.91f,0.16f,-0.10f},0.10f,-0.35f,0.92f,0.94f,true,false },
    { "Heat Burst", "HEAT", 16,3,18, LV_MID,10,1.65f,29,23,0.04f,0.03f,FX_NORMAL,0.35f,false,BOTH_HANDS,
      {-0.1f,0.25f,0}, {0.65f,-0.1f,0}, -0.15f,0.42f,0.91f,0.88f,true,true },
    { "Heat Smash", "HEAT x2", 18,4,30, LV_MID,34,1.85f,0,36,0,0.08f,FX_KNOCKDOWN,0.65f,false,R_HAND,
      {-0.22f,-0.15f,0.05f}, {0.72f,-0.05f,-0.15f},-0.18f,0.50f,0.88f,0.85f,true,false },
    { "Drive Impact", "IMPACT", 26,3,35, LV_SPECIAL,22,1.7f,48,20,0.04f,0.12f,FX_WALLSPLAT,0.52f,false,R_HAND,
      {-0.30f,-0.08f,-0.05f}, {0.74f,0.05f,-0.14f},-0.18f,0.52f,0.88f,0.85f,false,true },
    { "Drive Reversal", "GUARD+H", 18,3,26, LV_SPECIAL,10,1.75f,0,14,0,0.12f,FX_KNOCKDOWN,0.38f,false,L_HAND,
      {0.06f,0.08f,0.12f}, {0.76f,-0.08f,0.12f},0.04f,0.38f,0.90f,0.90f,true,false },
};

// -----------------------------------------------------------------------------
// small helpers
// -----------------------------------------------------------------------------
static unsigned int g_rng = 0x2545f491u;

static int RandInt(int lo, int hi) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return lo + (int)(g_rng % (unsigned int)(hi - lo + 1));
}
static float Frand() { return RandInt(0, 10000) / 10000.0f; }
static float FrandS() { return Frand() * 2.0f - 1.0f; }

static Vector3 Fwd(const Fighter &f) { return { cosf(f.yaw), 0, -sinf(f.yaw) }; }
static Vector3 Side(const Fighter &f) { return { sinf(f.yaw), 0, cosf(f.yaw) }; }

static void Play(SoundId s) { A::Play(s); }

static void SetBanner(const char *t, int frames) {
    std::snprintf(G.banner, sizeof(G.banner), "%s", t);
    G.bannerT = frames;
}
static void SetMsg(Fighter &f, const char *m) { f.msg = m; f.msgT = 55; }

static void SpawnSparks(Vector3 p, Color c, int n, float speed) {
    for (int i = 0; i < n; i++) {
        Particle q;
        q.p = p;
        q.v = { FrandS() * speed, Frand() * speed * 0.9f + 0.01f, FrandS() * speed };
        q.life = 0.6f + Frand() * 0.4f;
        q.size = 0.03f + Frand() * 0.04f;
        q.c = c;
        q.kind = 0;
        G.parts.push_back(q);
    }
    G.flashes.push_back({ p, 0, c });
}

static void SpawnDust(Vector3 p, int n, float speed) {
    for (int i = 0; i < n; i++) {
        Particle q;
        q.p = { p.x + FrandS() * 0.2f, 0.03f + Frand() * 0.05f, p.z + FrandS() * 0.2f };
        q.v = { FrandS() * speed, Frand() * speed * 0.4f, FrandS() * speed };
        q.life = 0.5f + Frand() * 0.5f;
        q.size = 0.05f + Frand() * 0.06f;
        q.c = { 132, 120, 128, 255 };
        q.kind = 1;
        G.parts.push_back(q);
    }
}

// -----------------------------------------------------------------------------
// body-space helpers, shared with the renderer
// -----------------------------------------------------------------------------
Matrix FighterRoot(const Fighter &f) {
    const Pose &p = f.pose;
    float spin=0;
    if(f.state==ST_ATTACK && f.move==M_SPIN) {
        float phase=Clampf(float(f.moveFrame-5)/22.0f,0,1);
        spin=phase*phase*(3-2*phase)*2*PI;
    }
    if(f.state==ST_LAUNCH && f.tornadoUsed)spin=f.pos.y*3.2f;
    return Mul(MatRotateZ(p.tilt), MatRotateY(f.yaw+spin),
               MatTranslate(f.pos.x, f.pos.y + p.hipY, f.pos.z));
}

Vector3 LimbWorld(const Fighter &f, Limb limb) {
    const Pose &p = f.pose;
    Vector3 tdir = { sinf(p.lean), cosf(p.lean), 0 };
    Vector3 chest = tdir * 0.50f;
    Vector3 local;
    switch (limb) {
        case L_HAND: local = chest + Vector3{ 0, -0.02f, -0.21f } + p.lHand; break;
        case R_HAND: local = chest + Vector3{ 0, -0.02f,  0.21f } + p.rHand; break;
        case L_FOOT: local = Vector3{ 0, -0.05f, -0.11f } + p.lFoot; break;
        case R_FOOT: local = Vector3{ 0, -0.05f,  0.11f } + p.rFoot; break;
        default:     local = chest + (p.lHand + p.rHand) * 0.5f; break;
    }
    return XformPoint(local, FighterRoot(f));
}

// -----------------------------------------------------------------------------
// input decoding
// -----------------------------------------------------------------------------
void ClearPressed(Input &in) {
    in.lpP = in.rpP = in.lkP = in.rkP = in.ssBg = in.ssFg = in.dashF = in.dashB = false;
    in.throwP = in.rageP = in.specialP = in.heatP = in.impactP = in.rushP = false;
    in.motion=0; in.enhanced=false;
    in.commandCaptured = false; in.commandDirections = 0;
}

void LatchInput(Input &pending, const Input &fresh) {
    Input old = pending;
    pending = fresh;
    bool attack = fresh.lpP || fresh.rpP || fresh.lkP || fresh.rkP || fresh.throwP || fresh.rageP || fresh.specialP || fresh.heatP || fresh.impactP;
    if (attack) {
        pending.commandCaptured = true;
        pending.commandDirections = fresh.commandCaptured ? fresh.commandDirections :
            (fresh.fwd?1u:0u) | (fresh.back?2u:0u) | (fresh.up?4u:0u) | (fresh.down?8u:0u);
        pending.throwP |= (fresh.lpP && fresh.lk) || (fresh.lkP && fresh.lp);
        pending.rageP |= (fresh.lpP && fresh.rp) || (fresh.rpP && fresh.lp);
        // The latest command replaces older attack buttons, without losing movement edges.
        old.lpP = old.rpP = old.lkP = old.rkP = old.throwP = old.rageP = old.specialP = old.heatP = old.impactP = false;
    } else {
        pending.motion=old.motion; pending.enhanced=old.enhanced;
        pending.commandCaptured = old.commandCaptured;
        pending.commandDirections = old.commandDirections;
    }
    pending.lpP |= old.lpP; pending.rpP |= old.rpP;
    pending.lkP |= old.lkP; pending.rkP |= old.rkP;
    pending.ssBg |= old.ssBg; pending.ssFg |= old.ssFg;
    pending.dashF |= old.dashF; pending.dashB |= old.dashB;
    pending.throwP |= old.throwP; pending.rageP |= old.rageP;
    pending.specialP |= old.specialP; pending.heatP |= old.heatP; pending.impactP |= old.impactP; pending.rushP |= old.rushP;
}

bool IsSpecial(int m) { return m>=M_WAVE && m<=M_SPIN; }
bool IsCancelableNormal(int m) { return m==M_LP || m==M_RP || m==M_LK || m==M_DLP || m==M_DLK; }

// Relative numpad notation: 2 down, 3 down-forward, 6 forward. Timed at 60 Hz,
// including hitstop, so a command entered during impact freeze survives intact.
void PrepareCommand(Fighter &f, Input &in) {
    ++f.inputClock;
    bool forward=in.fwd && !in.back, back=in.back && !in.fwd;
    bool down=in.down && !in.up, up=in.up && !in.down;
    int direction=5+(forward?1:back?-1:0)+(up?3:down?-3:0);
    if(f.historyCount==0 || f.historyDir[f.historyCount-1]!=direction) {
        if(f.historyCount==16){for(int i=1;i<16;++i){f.historyDir[i-1]=f.historyDir[i];f.historyTime[i-1]=f.historyTime[i];}--f.historyCount;}
        f.historyDir[f.historyCount]=direction; f.historyTime[f.historyCount++]=f.inputClock;
    }
    auto motion=[&](int a,int b,int c) {
        int steps[3]={a,b,c},wanted=2,last=-1;
        for(int i=f.historyCount-1;i>=0 && f.inputClock-f.historyTime[i]<=22;--i) {
            int d=f.historyDir[i];
            if(d==steps[wanted]){if(last<0)last=f.historyTime[i];if(--wanted<0)return f.inputClock-last<=6;}
            else if(d!=5 && wanted==2 && d!=6)return false;
        }
        return false;
    };
    if(in.lpP || in.rpP || in.lkP || in.rkP) {
        if((in.lpP || in.rpP) && motion(6,2,3))in.motion=2;
        else if((in.lpP || in.rpP) && motion(2,3,6))in.motion=1;
        else if((in.lkP || in.rkP) && motion(2,1,4))in.motion=3;
    }
    if(in.motion)in.enhanced = in.motion==3 ? in.lk && in.rk : in.lp && in.rp;
    if(in.specialP)in.enhanced=in.lp || in.rp || in.lk || in.rk;
}

static int ResolveMove(const Input &raw, const Fighter &f) {
    Input in = raw;
    if (in.commandCaptured) {
        in.fwd = (in.commandDirections & 1) != 0; in.back = (in.commandDirections & 2) != 0;
        in.up = (in.commandDirections & 4) != 0; in.down = (in.commandDirections & 8) != 0;
    }
    if (f.state == ST_JUMP || f.pos.y>0.08f) return (in.lkP || in.rkP) ? M_AIR_KICK : M_NONE;
    if(in.motion==2)return M_RISING;
    if(in.motion==1)return M_WAVE;
    if(in.motion==3)return M_SPIN;
    if(in.specialP)return in.down?M_RISING:in.back?M_SPIN:M_WAVE;
    if(in.heatP)return f.heatFrames>0?M_HEAT_SMASH:f.heatAvailable?M_HEAT_BURST:M_NONE;
    if(in.impactP)return f.state==ST_BLOCK?M_REVERSAL:M_IMPACT;
    if(in.back && in.lkP)return M_TORNADO;
    if (in.rageP && f.raging) return M_RAGE;
    if (in.throwP) return M_THROW;
    // rage art first: it shares LP+RP and only exists with a full meter
    if (f.raging && ((in.lpP && in.rp) || (in.rpP && in.lp))) return M_RAGE;
    if ((in.lpP && in.lk) || (in.lkP && in.lp)) return M_THROW;
    if (in.down) {
        if (in.lpP) return M_DLP;
        if (in.rpP) return M_DRP;
        if (in.lkP) return M_DLK;
        if (in.rkP) return M_DRK;
    }
    if (in.up && in.rkP) return M_UFRK;
    if (in.fwd && in.rpP) return M_FRP;
    if (in.fwd && in.lkP) return M_FLK;
    if (in.back && in.rpP) return M_BRP;
    // rising attack: pressing RP on the frame crouch is released
    if (f.state == ST_CROUCH && !in.down && in.rpP) return M_WRRP;
    if (in.lpP) return M_LP;
    if (in.rpP) return M_RP;
    if (in.lkP) return M_LK;
    if (in.rkP) return M_RK;
    return M_NONE;
}

// -----------------------------------------------------------------------------
// poses
// -----------------------------------------------------------------------------
static void Plant(Pose &p, bool left, bool right) {
    float y = -(p.hipY - 0.05f) + 0.06f;
    if (left) p.lFoot.y = y;
    if (right) p.rFoot.y = y;
}

static Pose BasePose(bool crouch, float t) {
    Pose p;
    if (!crouch) {
        float bob = sinf(t * 3.5f) * 0.012f;
        p.hipY = 0.92f + bob;
        p.lean = 0.12f;
        p.lHand = { 0.34f, -0.02f + bob, 0.08f };
        p.rHand = { 0.22f, -0.12f - bob, -0.06f };
        p.lFoot = { 0.24f, 0, -0.04f };
        p.rFoot = { -0.24f, 0, 0.06f };
    } else {
        p.hipY = 0.58f;
        p.lean = 0.38f;
        p.lHand = { 0.30f, 0.02f, 0.08f };
        p.rHand = { 0.22f, -0.04f, -0.06f };
        p.lFoot = { 0.28f, 0, -0.06f };
        p.rFoot = { -0.22f, 0, 0.08f };
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
            p.lFoot.x += s * 0.11f * f.walkSign;
            p.rFoot.x -= s * 0.11f * f.walkSign;
            p.lFoot.y += fmaxf(0, c) * 0.06f;
            p.rFoot.y += fmaxf(0, -c) * 0.06f;
            if (f.in.back) {
                p.lHand = { 0.26f, 0.10f, 0.10f };
                p.rHand = { 0.20f, 0.06f, -0.10f };
                p.lean = 0.02f;
            }
        }
        break;
    case ST_CROUCH: break;
    case ST_PREJUMP: p.hipY=0.72f;p.lean=0.25f;Plant(p,true,true);break;
    case ST_PARRY: case ST_PARRY_END: p.lHand={0.45f,0.15f,0.12f};p.rHand={0.38f,0.12f,-0.12f};break;
    case ST_RUSH: p.hipY=0.80f;p.lean=0.48f;p.lFoot.x=0.44f;Plant(p,true,true);break;
    case ST_TECH: p.hipY=0.46f;p.tilt=0.40f;p.lean=0.40f;Plant(p,true,true);break;
    case ST_LAND:
        p.hipY = 0.80f; p.lean = 0.24f;
        Plant(p, true, true);
        break;
    case ST_DASH:
        p.lean = f.dashDir > 0 ? 0.40f : -0.18f;
        p.hipY = 0.86f;
        p.lFoot.x = f.dashDir > 0 ? 0.40f : 0.10f;
        p.rFoot.x = f.dashDir > 0 ? -0.10f : -0.42f;
        Plant(p, true, true);
        break;
    case ST_SIDESTEP:
        p.hipY = 0.84f;
        p.lFoot.z = -0.26f; p.rFoot.z = 0.26f;
        p.lFoot.x = 0.10f;  p.rFoot.x = -0.10f;
        Plant(p, true, true);
        break;
    case ST_JUMP:
        p.hipY = 0.95f; p.lean = 0.2f;
        p.lFoot = { 0.22f, -0.55f, -0.05f };
        p.rFoot = { -0.08f, -0.60f, 0.05f };
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
        p.lHand = { 0.24f, 0.14f, 0.12f };
        p.rHand = { 0.20f, 0.10f, -0.12f };
        break;
    case ST_STAGGER:
        // knocked off balance: arms wide, weight back on the heels
        p.hipY = 0.86f; p.lean = -0.55f;
        p.lHand = { 0.02f, 0.22f, 0.30f };
        p.rHand = { -0.05f, 0.14f, -0.32f };
        Plant(p, true, true);
        break;
    case ST_HIT:
        p.hipY = 0.88f; p.lean = -0.45f;
        p.lHand = { 0.10f, -0.30f, -0.10f };
        p.rHand = { 0.05f, -0.32f, 0.10f };
        Plant(p, true, true);
        break;
    case ST_LAUNCH: case ST_THROWN:
        p.hipY = f.state == ST_THROWN ? 0.7f : 0.5f;
        p.tilt = f.state == ST_THROWN ? 1.9f : 1.3f;
        p.lean = -0.15f;
        p.lHand = { 0.25f, 0.15f, -0.20f };
        p.rHand = { 0.25f, 0.10f, 0.20f };
        p.lFoot = { 0.30f, -0.75f, -0.12f };
        p.rFoot = { 0.12f, -0.85f, 0.12f };
        break;
    case ST_DOWN: case ST_KO:
        p.hipY = 0.17f; p.tilt = PI * 0.5f; p.lean = 0.0f;
        p.lHand = { -0.10f, -0.35f, -0.22f };
        p.rHand = { -0.08f, -0.38f, 0.22f };
        p.lFoot = { -0.10f, -0.90f, -0.12f };
        p.rFoot = { 0.12f, -0.80f, 0.14f };
        break;
    case ST_GETUP:
        p.hipY = 0.50f; p.lean = 0.65f; p.tilt = 0;
        p.lHand = { 0.30f, -0.45f, 0.0f };
        p.rHand = { 0.25f, -0.45f, 0.0f };
        Plant(p, true, true);
        break;
    case ST_THROWING:
        if (f.moveFrame < 14)      { p.lHand = { 0.48f, 0.00f, 0.12f }; p.rHand = { 0.48f, 0.00f, -0.12f }; p.lean = 0.30f; }
        else if (f.moveFrame < 30) { p.lHand = { 0.20f, 0.50f, 0.10f }; p.rHand = { 0.20f, 0.50f, -0.10f }; p.lean = -0.25f; }
        else                       { p.lHand = { 0.50f, -0.40f, 0.10f }; p.rHand = { 0.50f, -0.40f, -0.10f }; p.lean = 0.55f; p.hipY = 0.8f; Plant(p, true, true); }
        break;
    case ST_WIN:
        p.lean = -0.05f;
        p.lHand = { 0.05f, 0.55f, -0.08f };
        p.rHand = { 0.10f, -0.15f, 0.05f };
        break;
    }
    return p;
}

static void BlendPose(Pose &a, const Pose &b, float k) {
    a.hipY = Lerpf(a.hipY, b.hipY, k);
    a.lean = Lerpf(a.lean, b.lean, k);
    a.tilt = Lerpf(a.tilt, b.tilt, k);
    a.lHand = VLerp(a.lHand, b.lHand, k);
    a.rHand = VLerp(a.rHand, b.rHand, k);
    a.lFoot = VLerp(a.lFoot, b.lFoot, k);
    a.rFoot = VLerp(a.rFoot, b.rFoot, k);
}

// -----------------------------------------------------------------------------
// trails
// -----------------------------------------------------------------------------
static void UpdateTrail(Fighter &f) {
    bool swinging = false;
    if (f.state == ST_ATTACK) {
        const MoveDef &mv = MOVES[f.move];
        swinging = f.moveFrame > mv.startup - 4 && f.moveFrame <= mv.startup + mv.active + 2;
    }
    if (swinging) {
        const MoveDef &mv = MOVES[f.move];
        Vector3 w = LimbWorld(f, mv.limb);
        if (f.trail.n < TRAIL_LEN) {
            f.trail.p[f.trail.n++] = w;
        } else {
            for (int i = 1; i < TRAIL_LEN; i++) f.trail.p[i - 1] = f.trail.p[i];
            f.trail.p[TRAIL_LEN - 1] = w;
        }
        f.trail.fade = 1.0f;
        bool foot = mv.limb == L_FOOT || mv.limb == R_FOOT;
        f.trail.c = (f.move == M_RAGE) ? Color{ 255, 80, 50, 255 }
                  : foot               ? Color{ 150, 200, 255, 255 }
                                       : Color{ 255, 210, 150, 255 };
    } else {
        f.trail.fade *= 0.78f;
        if (f.trail.fade < 0.02f) f.trail.n = 0;
    }
}

// -----------------------------------------------------------------------------
// combat
// -----------------------------------------------------------------------------
static void AddMeter(Fighter &f, int amount) {
    if (f.raging) return;
    f.meter = std::min(METER_MAX, f.meter + amount);
    if (f.meter >= METER_MAX) {
        f.raging = true;
        f.rageFlash = 40;
        SetMsg(f, "RAGE");
        Play(SND_RAGE);
    }
}

static void DrainDrive(Fighter &f,float amount) {
    if(f.burnout>0)return;
    f.drive=std::max(0.0f,f.drive-amount);f.driveWait=100;
    if(f.drive<=0.0001f){f.drive=0;f.burnout=600;SetMsg(f,"BURNOUT");}
}
static bool SpendDrive(Fighter &f,float cost) {
    if(f.burnout || f.drive+0.0001f<cost){SetMsg(f,"DRIVE EMPTY");return false;}
    DrainDrive(f,cost);return true;
}
static void RecoverHealth(Fighter &f,int amount) {
    if(f.ko)return;
    int heal=std::min(f.recoverable,amount);f.hp=std::min(MAX_HP,f.hp+heal);f.recoverable-=heal;
}
static bool StartRush(Fighter &f,int cost) {
    if(cost && !SpendDrive(f,float(cost)))return false;
    f.state=ST_RUSH;f.timer=18;f.crouched=false;f.rushReady=45;f.velocity={};f.buf=f.bufT=0;
    SetMsg(f,cost==0?"HEAT DASH":"DRIVE RUSH");SpawnDust(f.pos,8,0.04f);return true;
}

static void DealDamage(Fighter &a, Fighter &d, int dmg) {
    if(d.state==ST_LAUNCH || d.state==ST_DOWN)d.recoverable+=dmg/2;
    d.hp -= dmg;
    d.recoverable=std::min(d.recoverable,MAX_HP-std::max(0,d.hp));
    RecoverHealth(a,std::max(1,dmg/4));
    d.driveWait=100;
    d.flash = 1.0f;
    d.comboCount++;
    d.comboDmg += dmg;
    a.showCount = d.comboCount;
    a.showDmg = d.comboDmg;
    a.showT = 85;
    // the fighter taking punishment builds rage faster - a comeback valve
    AddMeter(a, (int)(dmg * 0.8f));
    AddMeter(d, (int)(dmg * 1.4f));
    if (d.hp <= 0) { d.hp = 0; d.ko = true; }
}

static void StartMove(Fighter &f, float wantYaw, int m, int skip) {
    if (m == M_AIR_KICK && f.pos.y <= 0) m = M_LK;
    bool ex=IsSpecial(m) && f.bufEnhanced;
    f.bufEnhanced=false;
    if(m==M_WAVE)for(const auto &p:G.projectiles)if(p.owner==f.id && p.life>0){f.buf=f.bufT=0;return;}
    float cost=ex?2.0f:m==M_IMPACT?1.0f:m==M_REVERSAL?2.0f:0.0f;
    if((cost && !SpendDrive(f,cost)) || (m==M_HEAT_BURST && !f.heatAvailable) ||
       (m==M_HEAT_SMASH && f.heatFrames<=0)) {f.buf=f.bufT=0;return;}
    f.enhanced=ex;f.armorHits=0;
    f.rushBonus=f.rushReady>0 && IsCancelableNormal(m)?4:0;f.rushReady=0;
    f.historyCount=0;
    if(m==M_HEAT_BURST){f.heatAvailable=false;f.heatFrames=f.heatDuration=600;SetMsg(f,"HEAT BURST");}
    if(m==M_HEAT_SMASH){f.heatFrames=0;SetMsg(f,"HEAT SMASH");}
    if(ex)SetMsg(f,"OVERDRIVE");
    f.yaw = wantYaw;
    if (f.pos.y <= 0) f.velocity = {};
    f.contact = 0;
    f.state = ST_ATTACK;
    f.move = m;
    f.moveFrame = skip;
    f.moveHit = false;
    f.serial++;
    f.crouched = MOVES[m].crouching;
    f.buf = 0;
    f.bufT = 0;
    f.armorUsed = false;
    if (m == M_RAGE) {
        f.meter = 0;
        f.raging = false;
        f.rageFlash = 60;
        G.screenFlash = fmaxf(G.screenFlash, 0.30f);
        G.flashCol = { 1.0f, 0.35f, 0.25f };
        Play(SND_RAGE);
    }
}

static int ActiveMove(const Fighter &f) {
    if (f.state != ST_ATTACK || f.moveHit || f.move==M_WAVE) return 0;
    const MoveDef &mv = MOVES[f.move];
    return (f.moveFrame > mv.startup && f.moveFrame <= mv.startup + mv.active) ? f.move : 0;
}

CombatVolume HurtVolume(const Fighter &f) {
    bool prone = f.state == ST_DOWN || f.state == ST_KO;
    float height = prone ? 0.36f : f.crouched ? 1.0f : 1.78f;
    return {f.pos + Vector3{0,height*0.5f,0},
            {prone?0.70f:0.32f,height*0.5f,0.28f},f.yaw};
}

CombatVolume AttackVolume(const Fighter &f, int move) {
    const MoveDef &m = MOVES[move];
    float near = 0.18f, far = std::max(near+0.1f,m.range-0.30f);
    float height = m.level==LV_LOW ? 0.24f : m.level==LV_HIGH ? 1.43f : 0.98f;
    if (move == M_AIR_KICK) height = 0.60f;
    Vector3 center = f.pos + Fwd(f)*((near+far)*0.5f);
    center.y += height;
    return {center,{(far-near)*0.5f,m.level==LV_THROW?0.50f:0.19f,m.homing?0.48f:0.22f},f.yaw};
}

bool CombatOverlap(const CombatVolume &a,const CombatVolume &b) {
    if (fabsf(a.center.y-b.center.y)>a.half.y+b.half.y) return false;
    Vector3 af{cosf(a.yaw),0,-sinf(a.yaw)}, as{sinf(a.yaw),0,cosf(a.yaw)};
    Vector3 bf{cosf(b.yaw),0,-sinf(b.yaw)}, bs{sinf(b.yaw),0,cosf(b.yaw)};
    Vector3 delta=b.center-a.center;
    // Separating-axis test: four axes cover two oriented rectangles on the mat.
    for (Vector3 axis : {af,as,bf,bs}) {
        float ra=fabsf(VDot(af,axis))*a.half.x+fabsf(VDot(as,axis))*a.half.z;
        float rb=fabsf(VDot(bf,axis))*b.half.x+fabsf(VDot(bs,axis))*b.half.z;
        if (fabsf(VDot(delta,axis))>ra+rb) return false;
    }
    return true;
}

// Read collision and defensive decisions from one shared pre-contact snapshot.
// Both active strikes can trade; resolving P1 first cannot erase P2's strike.
static bool ResolveHit(Fighter &a, Fighter &d, int moveId,
                       const Fighter &beforeA, const Fighter &beforeD, const Projectile *projectile=nullptr) {
    if(beforeD.ko)return false;
    MoveDef mv = MOVES[moveId];
    if(beforeA.enhanced && IsSpecial(moveId)){mv.damage=mv.damage*13/10;mv.hitstun+=4;mv.blockstun+=2;}
    mv.hitstun+=beforeA.rushBonus;mv.blockstun+=beforeA.rushBonus;
    bool ownMove=!projectile || a.serial==projectile->serial;
    if(beforeD.state==ST_TECH && beforeD.timer>8)return false;
    bool reversal=beforeD.state==ST_ATTACK && beforeD.move==M_REVERSAL && beforeD.moveFrame<=12;
    bool rising=beforeD.state==ST_ATTACK && beforeD.move==M_RISING && beforeD.moveFrame<=10;
    if(reversal || (rising && (beforeD.enhanced || (!projectile && beforeA.pos.y>0.1f))))return false;
    if (beforeD.state == ST_KO || (beforeD.state == ST_GETUP && beforeD.timer>8) || beforeD.state == ST_THROWN ||
        beforeD.state == ST_THROWING || beforeD.state == ST_WIN) return false;
    if (beforeA.state == ST_THROWN || beforeA.state == ST_THROWING) return false;

    Vector3 fw = Fwd(beforeA), sd = Side(beforeA);
    Vector3 rel = { beforeD.pos.x - beforeA.pos.x, 0, beforeD.pos.z - beforeA.pos.z };
    float dx = VDot(rel, fw), dz = VDot(rel, sd);
    // homing moves track through a sidestep; everything else whiffs on it
    CombatVolume volume=projectile?CombatVolume{projectile->pos,{0.28f,PROJECTILE_HALF_HEIGHT,0.28f},beforeA.yaw}:AttackVolume(beforeA,moveId);
    if (!CombatOverlap(volume,HurtVolume(beforeD))) return false;

    bool air = beforeD.state == ST_LAUNCH || beforeD.state == ST_JUMP || beforeD.pos.y > 0.08f;
    bool crouch = beforeD.crouched && (beforeD.state == ST_CROUCH || beforeD.state == ST_ATTACK || beforeD.state == ST_BLOCK);

    if (mv.level == LV_THROW) {
        if (d.throwInvul>0 || air || crouch || d.state == ST_HIT || d.state == ST_BLOCK || d.state == ST_DOWN) return false;
        a.moveHit = true;
        a.state = ST_THROWING;
        a.moveFrame = 0;
        d.state = ST_THROWN;
        d.crouched = false;
        d.kb = d.velocity = { 0, 0, 0 };
        a.velocity = {};
        d.escapeT = 12;                 // window to break out with a punch
        if(beforeD.state==ST_PARRY){DrainDrive(d,1.0f);SetMsg(a,"PARRY PUNISH");}
        else SetMsg(a, "THROW");
        Play(SND_WHOOSH);
        return true;
    }
    if (mv.level == LV_HIGH && crouch) return false;
    if (d.state == ST_DOWN && mv.level != LV_LOW) return false;
    if (air && mv.level == LV_LOW && d.pos.y > 0.25f) return false;
    if (air && d.pos.y > 1.7f) return false;
    if (a.pos.y > d.pos.y + 1.35f) return false;

    if(ownMove){a.moveHit = true; a.contact = 1;}
    float len = VLen(rel);
    Vector3 dir = len > 0.01f ? rel * (1 / len) : fw;
    float h = mv.level == LV_HIGH ? 1.45f : (mv.level == LV_MID || mv.level == LV_SPECIAL) ? 1.05f : 0.30f;
    if (air) h = d.pos.y + 0.6f;
    if (d.state == ST_DOWN) h = 0.2f;
    Vector3 sp = beforeA.pos + fw * fminf(dx, mv.range) * 0.85f;
    sp.y = h;

    if(beforeD.lowParryWindow>0 && mv.level==LV_LOW &&
       (beforeD.state==ST_IDLE || beforeD.state==ST_CROUCH)) {
        d.lowParryWindow=0;d.state=ST_IDLE;d.crouched=false;d.velocity={};
        a.state=ST_LAUNCH;a.vy=6.8f;a.kb=dir*(-0.9f);a.juggle=2;a.tornadoUsed=true;
        a.contact=3;G.hitstop=8;SetMsg(d,"LOW PARRY");Play(SND_PARRY);return true;
    }
    if(beforeD.state==ST_PARRY && !beforeD.burnout) {
        bool perfect=beforeD.parryPerfect>0;
        d.drive=std::min(6.0f,d.drive+0.65f);d.parryPerfect=0;
        if(ownMove)a.contact=3;
        if(perfect){d.state=ST_IDLE;d.timer=0;d.perfectScale=150;
            if(!projectile){a.state=ST_STAGGER;a.timer=22;a.velocity={};}}
        SpawnSparks(sp,{111,215,255,255},perfect?18:7,0.055f);
        SetMsg(d,perfect?"PERFECT PARRY":"DRIVE PARRY");G.hitstop=perfect?9:3;Play(SND_PARRY);return true;
    }

    // Drive Impact absorbs two strikes. Other power crushes absorb one.
    if (beforeD.state == ST_ATTACK && MOVES[beforeD.move].armored &&
        beforeD.armorHits < (beforeD.move==M_IMPACT?2:1) &&
        (mv.level!=LV_LOW || beforeD.move==M_IMPACT || beforeD.move==M_RAGE) &&
        beforeD.moveFrame <= MOVES[beforeD.move].startup+(beforeD.move==M_IMPACT?MOVES[beforeD.move].active:0)) {
        if(ownMove)a.contact = 4;
        d.armorUsed = true;d.armorHits++;
        DealDamage(a, d, std::max(1, mv.damage / 2));
        SpawnSparks(sp, { 255, 90, 60, 255 }, 12, 0.06f);
        if(d.ko){d.state=ST_LAUNCH;d.vy=5;d.kb=dir*3;}
        G.hitstop = 5;
        G.shake = fmaxf(G.shake, 0.09f);
        SetMsg(d, "ARMOR");
        Play(SND_BLOCK);
        return true;
    }

    // ---- guard ----
    bool guarding = (beforeD.state == ST_IDLE || beforeD.state == ST_CROUCH || beforeD.state == ST_BLOCK || beforeD.state==ST_PARRY_END) &&
                    d.in.back && !d.in.fwd;
    bool blocked = guarding && (mv.level==LV_SPECIAL || (mv.level == LV_LOW ? d.crouched : !d.crouched));
    if (blocked) {
        if(ownMove){a.contact = 2;
            a.frameAdvantage = mv.blockstun - (mv.startup + mv.active + mv.recovery - beforeA.moveFrame)+(d.burnout?4:0);}
        d.velocity = {};
        if (d.crouched) d.lowBlocks++; else d.highBlocks++;

        d.state = ST_BLOCK;
        d.timer = mv.blockstun+(d.burnout?4:0);
        DrainDrive(d,mv.damage>=16?0.35f:0.16f);
        RecoverHealth(a,2);
        d.kb = dir * (mv.pushBlock * 60.0f);
        if(!projectile)a.kb = dir * (-mv.pushBlock * 24.0f);
        if (IsSpecial(moveId) || beforeA.heatFrames>0 || moveId==M_HEAT_SMASH || (d.burnout && mv.damage>=16)) {
            int chip=std::max(1,mv.damage/7),oldHP=d.hp;
            d.hp=std::max(d.burnout?0:1,d.hp-chip);d.recoverable+=oldHP-d.hp;
            if(!d.hp){d.ko=true;d.state=ST_KO;}
        }
        if(moveId==M_IMPACT && VLen(Vector3{d.pos.x,0,d.pos.z})>ARENA_R-0.75f && !d.ko){
            d.state=ST_STAGGER;d.timer=d.burnout?75:38;d.kb={};d.wallSplat=true;
            SetMsg(a,d.burnout?"BURNOUT STUN":"WALL SPLAT");
        }
        AddMeter(d, 3);
        AddMeter(a, 2);
        SpawnSparks(sp, { 170, 210, 255, 255 }, 6, 0.04f);
        G.hitstop = 3;
        Play(SND_BLOCK);
        return true;
    }

    int dmg = mv.damage;
    if(a.perfectScale>0)dmg=std::max(1,dmg/2);
    bool counter = beforeD.state == ST_ATTACK && beforeD.moveFrame <= MOVES[beforeD.move].startup;
    bool punish=beforeD.state==ST_ATTACK && beforeD.moveFrame>MOVES[beforeD.move].startup+MOVES[beforeD.move].active;
    if(punish){dmg=dmg*12/10;mv.hitstun+=6;SetMsg(a,"PUNISH COUNTER");}
    if (counter) { dmg = dmg * 12 / 10 + 1; SetMsg(a, "COUNTER"); }
    bool heavy = dmg >= 13;
    d.velocity = {};
    if(ownMove){a.frameAdvantage = mv.hitstun + (counter?4:0) - (mv.startup + mv.active + mv.recovery - beforeA.moveFrame);
        if (air || mv.effect==FX_LAUNCH || mv.effect==FX_KNOCKDOWN || mv.effect==FX_SWEEP) a.frameAdvantage=999;}

    if (d.state == ST_DOWN) {
        DealDamage(a, d, std::max(1, dmg / 2));
        if (d.ko) d.state = ST_KO;
    } else {
        if (beforeD.state == ST_IDLE || beforeD.state == ST_CROUCH || beforeD.state == ST_ATTACK ||
            beforeD.state == ST_DASH || beforeD.state == ST_SIDESTEP || beforeD.state == ST_JUMP ||
            (beforeD.state == ST_STAGGER && !beforeD.wallSplat) || beforeD.state == ST_LAND || beforeD.state==ST_PARRY || beforeD.state==ST_RUSH || beforeD.state==ST_PREJUMP || beforeD.state==ST_PARRY_END) {
            d.comboCount = 0;
            d.comboDmg = 0;
        }
        // Scaling applies to grounded strings as well as juggles.
        static const float scale[6] = {1.0f,0.90f,0.78f,0.65f,0.52f,0.40f};
        dmg = std::max(1, int(dmg * scale[std::min(d.comboCount,5)]));
        DealDamage(a, d, dmg);
        d.crouched = false;
        if (d.ko) {
            d.state = ST_LAUNCH; d.vy = 6.6f; d.kb = dir * 4.2f;
        } else if (air && mv.effect==FX_TORNADO && !d.tornadoUsed) {
            d.state=ST_LAUNCH;d.tornadoUsed=true;d.vy=6.7f;d.kb=dir*2.0f;
            d.juggle=std::max(2,d.juggle);a.frameAdvantage=999;SetMsg(a,"TORNADO");
        } else if (air) {
            d.state = ST_LAUNCH;
            d.vy = d.juggle >= 5 ? fminf(d.vy, -1.0f) : fmaxf(1.0f, 5.1f - 0.85f * d.juggle);
            d.kb = dir * 2.7f;
            d.juggle++;
        } else if (mv.effect == FX_LAUNCH) {
            d.state = ST_LAUNCH; d.vy = 9.0f; d.kb = dir * 1.32f; d.juggle = 1;
            SetMsg(a, "LAUNCH");
        } else if (mv.effect == FX_KNOCKDOWN) {
            d.state = ST_LAUNCH; d.vy = 4.8f; d.kb = dir * 5.1f; d.juggle = 2;
        } else if (mv.effect == FX_SWEEP) {
            d.state = ST_LAUNCH; d.vy = 3.6f; d.kb = dir * 1.2f; d.juggle = 2;
        } else if (moveId==M_IMPACT) {
            d.state=ST_STAGGER;d.timer=48;d.wallSplat=true;d.kb=dir*1.2f;
            a.frameAdvantage=10;SetMsg(a,"CRUMPLE");DrainDrive(d,0.75f);
        } else if (mv.effect == FX_WALLSPLAT) {
            a.frameAdvantage = 32 + (counter?4:0) - (mv.startup + mv.active + mv.recovery - beforeA.moveFrame);
            d.state = ST_HIT; d.timer = 32 + (counter ? 4 : 0); d.kb = dir * ((mv.pushHit + 0.14f)*60.0f);
        } else {
            d.state = ST_HIT;
            d.timer = mv.hitstun + (counter ? 4 : 0);
            d.kb = dir * (mv.pushHit*60.0f);
        }
    }
    if(moveId==M_FRP && a.heatAvailable && !air && !d.ko && beforeD.state!=ST_DOWN) {
        a.heatAvailable=false;a.heatFrames=a.heatDuration=900;
        StartRush(a,0);a.rushReady=45;d.timer=42;a.frameAdvantage=24;SetMsg(a,"HEAT ENGAGER");
    }
    SpawnSparks(sp, heavy ? Color{ 255, 170, 60, 255 } : Color{ 255, 235, 140, 255 },
                heavy ? 16 : 9, heavy ? 0.075f : 0.05f);
    G.hitstop = heavy ? 8 : 5;
    G.shake = fmaxf(G.shake, heavy ? 0.12f : 0.05f);
    G.camPunch = fmaxf(G.camPunch, heavy ? 0.55f : 0.22f);
    G.camRoll += (heavy ? 0.045f : 0.018f) * (a.id == 0 ? 1.0f : -1.0f);
    G.screenFlash = fmaxf(G.screenFlash, heavy ? 0.16f : 0.07f);
    G.flashCol = { 1.0f, 0.82f, 0.55f };
    Play(heavy ? SND_HEAVY : SND_HIT);

    if (moveId == M_RAGE) {
        G.hitstop = 22;
        G.slowmo = std::max(G.slowmo, 40);
        G.shake = 0.28f;
        G.screenFlash = 0.45f;
        G.flashCol = { 1.0f, 0.4f, 0.28f };
    }
    if (d.ko) {
        G.slowmo = 96;
        G.hitstop = 14;
        G.shake = 0.25f;
        G.camPunch = 0.95f;
        G.screenFlash = 0.40f;
        G.flashCol = { 1.0f, 0.65f, 0.45f };
        Play(SND_KO);
    }
    return true;
}

static void TurnToward(Fighter &f, float want, float rate) {
    float d = AngleDiff(f.yaw, want);
    f.yaw += Clampf(d, -rate, rate);
}

static Vector3 ApproachVelocity(Vector3 current, Vector3 target, float acceleration) {
    Vector3 delta = target-current;
    float length = VLen(delta), step = acceleration*DT;
    return length <= step ? target : current + delta*(step/length);
}

static void Land(Fighter &f) {
    f.pos.y = 0; f.vy = 0; f.jumpVx = 0;
    f.state = ST_LAND; f.timer = 4;
    f.velocity = f.velocity * 0.30f;
    SpawnDust(f.pos, 5, 0.025f);
}

static void UpdateFighter(Fighter &f, Fighter &o, const Fighter &opSnapshot, const Input &in) {
    f.in = in;
    Vector3 rel = { opSnapshot.pos.x - f.pos.x, 0, opSnapshot.pos.z - f.pos.z };
    float dist = VLen(rel);
    Vector3 dir = dist > 0.001f ? rel * (1 / dist) : Fwd(f);
    float wantYaw = atan2f(-dir.z, dir.x);

    int m = ResolveMove(in, f);
    if (m) { f.buf = m; f.bufT = 12; f.bufEnhanced=in.enhanced; }
    else if (f.bufT > 0 && --f.bufT == 0) f.buf = 0;

    // Resource clocks advance with the simulation, never during hitstop or pause.
    if(f.driveWait>0)--f.driveWait;
    if(f.burnout>0){if(--f.burnout==0){f.drive=6;SetMsg(f,"DRIVE RESTORED");}}
    else if(f.driveWait==0 && !m && !in.rushP && !in.parry && (f.state==ST_IDLE || f.state==ST_CROUCH || f.state==ST_DASH || f.state==ST_SIDESTEP))
        f.drive=std::min(6.0f,f.drive+0.55f*DT);
    if(f.heatFrames>0 && f.state!=ST_ATTACK && f.state!=ST_HIT && f.state!=ST_LAUNCH)--f.heatFrames;
    if(f.rushReady>0)--f.rushReady;
    if(f.perfectScale>0)--f.perfectScale;
    if(f.parryPerfect>0)--f.parryPerfect;
    if(f.throwInvul>0)--f.throwInvul;
    if(f.techWindow>0)--f.techWindow;
    if(f.lowParryWindow>0)--f.lowParryWindow;
    if(f.lowParryCooldown>0)--f.lowParryCooldown;
    bool df=in.down && in.fwd && !in.back;
    if(df && !f.downForwardHeld && !f.lowParryCooldown){f.lowParryWindow=6;f.lowParryCooldown=20;}
    f.downForwardHeld=df;
    f.parryWindow=0;f.guardHeld=in.back && !in.fwd;
    if(f.state==ST_LAUNCH && !f.ko && f.vy<0 && f.pos.y<0.5f && (in.lpP || in.rpP || in.ssBg || in.ssFg)) {
        f.techWindow=10;f.ssDir=in.ssBg?1:in.ssFg?-1:0;
    }

    f.walking = false;
    f.flash *= 0.85f;
    if (f.msgT > 0) f.msgT--;
    if (f.showT > 0) f.showT--;
    if (f.rageFlash > 0) f.rageFlash--;
    if (f.escapeT > 0) f.escapeT--;
    f.hpLag = f.hpLag > f.hp ? fmaxf((float)f.hp, f.hpLag - 0.45f) : (float)f.hp;

    bool confirmed=f.contact==1 || f.contact==2 || f.contact==4;
    bool cancelWindow=f.state==ST_ATTACK && f.moveHit && confirmed && f.moveFrame>=MOVES[f.move].startup &&
        f.moveFrame<=MOVES[f.move].startup+MOVES[f.move].active+7 && f.pos.y<=0.08f;
    if(cancelWindow && f.move==M_FRP && f.heatFrames>0 && (in.rushP || in.dashF || in.heatP)) {
        f.heatFrames=0;StartRush(f,0);return;
    }
    if(cancelWindow && IsCancelableNormal(f.move)) {
        if((in.rushP || in.dashF) && StartRush(f,3))return;
        if(IsSpecial(f.buf) || f.buf==M_IMPACT || f.buf==M_RAGE) {StartMove(f,wantYaw,f.buf,0);return;}
    }
    if(cancelWindow && IsSpecial(f.move) && f.buf==M_RAGE){StartMove(f,wantYaw,M_RAGE,0);return;}

    switch (f.state) {
    case ST_IDLE: case ST_CROUCH:
        TurnToward(f, wantYaw, 0.3f);
        f.comboCount = 0; f.comboDmg = 0; f.juggle = 0; f.wallSplat = false; f.tornadoUsed=false;
        if (f.buf) { StartMove(f, wantYaw, f.buf, 0); break; }
        if((in.rushP || (in.parry && in.dashF)) && StartRush(f,1))break;
        if(in.parry && !f.burnout && f.drive>=0.5f){
            SpendDrive(f,0.5f);f.state=ST_PARRY;f.parryPerfect=2;f.velocity={};f.crouched=false;break;
        }
        if (in.ssBg || in.ssFg) { f.state = ST_SIDESTEP; f.timer = 14; f.ssDir = in.ssBg ? 1 : -1; f.crouched = false; break; }
        if (in.dashF) { f.state = ST_DASH; f.timer = 12; f.dashDir = 1;  f.crouched = false; break; }
        if (in.dashB) { f.state = ST_DASH; f.timer = 14; f.dashDir = -1; f.crouched = false; break; }
        if (in.up) {
            f.state = ST_PREJUMP; f.timer=3;f.crouched = false;
            f.jumpVx = in.fwd ? 3.0f : in.back ? -2.7f : 0.0f;
            f.velocity = {};
            break;
        }
        if (in.down) { f.state = ST_CROUCH; f.crouched = true; f.velocity = ApproachVelocity(f.velocity, {}, 48.0f); }
        else {
            f.state = ST_IDLE; f.crouched = false;
            if (in.fwd && !in.back) {
                f.velocity = ApproachVelocity(f.velocity, dir * (dist > BODY_DIST + 0.02f ? 2.88f : 0.0f), 34.0f);
                f.walking = true; f.walkSign = 1; f.walkPhase += 0.26f;
            } else if (in.back && !in.fwd) {
                if (!(G.mode == 3 && f.id == 1 && G.dummy != 3))
                    f.velocity = ApproachVelocity(f.velocity, dir * -2.16f, 38.0f);
                f.walking = true; f.walkSign = -1; f.walkPhase += 0.22f;
            } else f.velocity = ApproachVelocity(f.velocity, {}, 48.0f);
        }
        break;

    case ST_PREJUMP:
        if(--f.timer<=0){f.state=ST_JUMP;f.vy=JUMP_SPEED;f.velocity=Fwd(f)*f.jumpVx;f.pos.y=0.001f;}
        break;
    case ST_PARRY:
        f.velocity={};
        if((in.rushP || in.dashF) && StartRush(f,1))break;
        if(!in.parry || f.burnout){f.state=ST_PARRY_END;f.timer=16;f.parryPerfect=0;break;}
        DrainDrive(f,0.8f*DT);break;
    case ST_PARRY_END:
        f.velocity={};
        if(--f.timer<=0)f.state=ST_IDLE;
        break;
    case ST_RUSH:
        TurnToward(f,wantYaw,0.22f);
        f.velocity=dir*(8.4f*Clampf(f.timer/6.0f,0.3f,1.0f));
        if(f.timer<15 && f.buf){StartMove(f,wantYaw,f.buf,0);break;}
        if(--f.timer<=0){f.state=ST_IDLE;f.velocity={};}
        break;
    case ST_TECH:
        f.velocity=f.ssDir?G.bgDir*(f.ssDir*3.4f):dir*-2.8f;
        if(--f.timer<=0){f.state=ST_IDLE;f.velocity={};f.throwInvul=6;}
        break;
    case ST_DASH:
        if(f.dashDir<0 && f.timer<12 && in.down){f.state=ST_CROUCH;f.crouched=true;f.velocity=f.velocity*0.25f;break;}
        TurnToward(f, wantYaw, 0.3f);
        {
            float speed = f.dashDir > 0 ? 7.2f : -6.0f;
            float envelope = Clampf(f.timer / 5.0f, 0.3f, 1.0f);
            f.velocity = ApproachVelocity(f.velocity, dir*(speed*envelope), 90.0f);
        }
        if (f.timer < 9 && in.back && !in.fwd && f.dashDir > 0) {
            f.state = ST_IDLE; f.velocity = {}; break;
        }
        if (f.dashDir > 0 && f.buf && f.timer < 8) { StartMove(f, wantYaw, f.buf, 0); break; }
        if (--f.timer <= 0) f.state = ST_IDLE;
        break;

    case ST_SIDESTEP:
        if(f.timer<=10 && f.buf){StartMove(f,wantYaw,f.buf,0);break;}
        if(f.timer<=8 && in.back){f.state=ST_IDLE;f.velocity={};break;}
        TurnToward(f, wantYaw, 0.35f);
        f.velocity = ApproachVelocity(f.velocity, G.bgDir * (f.ssDir * 4.92f * Clampf(f.timer/4.0f,0.25f,1.0f)), 70.0f);
        if (--f.timer <= 0) f.state = ST_IDLE;
        break;

    case ST_JUMP:
        if (f.buf == M_AIR_KICK) { StartMove(f, wantYaw, M_AIR_KICK, 0); break; }
        f.pos.y += f.vy*DT - 0.5f*GRAVITY*DT*DT;
        f.vy -= GRAVITY*DT;
        if (f.pos.y <= 0 && f.vy < 0) {
            Land(f);
        }
        break;

    case ST_ATTACK: {
        const MoveDef &mv = MOVES[f.move];
        if (f.moveFrame <= 3 && f.buf == M_THROW && f.move != M_THROW) { StartMove(f, f.yaw, M_THROW, f.moveFrame); break; }
        f.moveFrame++;
        if(mv.homing && f.moveFrame<=mv.startup)TurnToward(f,wantYaw,0.18f);
        if(f.move==M_WAVE && f.moveFrame==mv.startup+1) {
            bool exists=false;for(const auto &p:G.projectiles)exists|=p.owner==f.id && p.life>0;
            if(!exists)G.projectiles.push_back({f.pos+Fwd(f)*0.55f+Vector3{0,PROJECTILE_HEIGHT,0},Fwd(f)*(f.enhanced?12.0f:9.0f),f.id,f.serial,105,f.enhanced});
            Play(SND_WHOOSH);
        }
        if(f.move==M_RISING && f.moveFrame==mv.startup){f.vy=6.8f;f.pos.y=0.001f;f.velocity=Fwd(f)*1.6f;}
        if (f.pos.y > 0) {
            f.pos.y += f.vy*DT - 0.5f*GRAVITY*DT*DT;
            f.vy -= GRAVITY*DT;
            if (f.pos.y <= 0) { Land(f); f.timer = 7; break; }
        }
        if (f.moveFrame <= mv.startup && dist > BODY_DIST && f.pos.y <= 0) {
            // Concentrate root motion near impact instead of sliding at a constant speed.
            float u = float(f.moveFrame)/mv.startup, prev = float(f.moveFrame-1)/mv.startup;
            f.pos = f.pos + Fwd(f)*(mv.lunge*(u*u-prev*prev));
        }
        if (f.moveFrame == mv.startup) Play(SND_WHOOSH);
        // strings: LP -> RP -> LK, and LK -> RK
        if (f.moveHit && f.moveFrame > mv.startup + mv.active && f.moveFrame <= mv.startup + mv.active + 8) {
            if (f.move == M_LP && f.buf == M_RP) { StartMove(f, f.yaw, M_RP, 4); break; }
            if (f.move == M_RP && f.buf == M_LK) { StartMove(f, f.yaw, M_LK, 4); break; }
            if (f.move == M_LK && f.buf == M_RK) { StartMove(f, f.yaw, M_RK, 6); break; }
        }
        if (f.moveFrame >= mv.startup + mv.active + mv.recovery) {
            f.state = f.pos.y > 0 ? ST_JUMP : (mv.crouching && in.down) ? ST_CROUCH : ST_IDLE;
            f.crouched = f.state == ST_CROUCH;
        }
        break; }

    case ST_LAND:
        f.velocity = ApproachVelocity(f.velocity, {}, 48.0f);
        if (--f.timer <= 0) f.state = ST_IDLE;
        break;

    case ST_HIT:
        if (--f.timer <= 0) f.state = ST_IDLE;
        break;

    case ST_STAGGER:
        if (--f.timer <= 0) f.state = ST_IDLE;
        break;

    case ST_BLOCK:
        if(f.buf==M_REVERSAL){StartMove(f,wantYaw,M_REVERSAL,0);break;}
        TurnToward(f, wantYaw, 0.3f);
        f.crouched = in.down;
        if (--f.timer <= 0) f.state = f.crouched ? ST_CROUCH : ST_IDLE;
        break;

    case ST_LAUNCH:
        f.pos.y += f.vy*DT - 0.5f*(GRAVITY + 4.3f*f.juggle)*DT*DT;
        f.vy -= (GRAVITY + 4.3f*f.juggle)*DT;
        if (f.pos.y <= 0 && f.vy < 0) {
            f.pos.y = 0; f.vy = 0;
            f.kb = f.kb * 0.4f;
            if(f.techWindow>0 && !f.ko){f.state=ST_TECH;f.timer=16;f.throwInvul=10;SetMsg(f,"TECH ROLL");}
            else {f.state = f.ko ? ST_KO : ST_DOWN;f.timer=36;}
            SpawnSparks({ f.pos.x, 0.05f, f.pos.z }, { 150, 140, 130, 255 }, 8, 0.03f);
            SpawnDust(f.pos, 10, 0.03f);
            if (!G.flashes.empty()) G.flashes.pop_back();
            G.shake = fmaxf(G.shake, 0.06f);
            Play(SND_THUD);
        }
        break;

    case ST_DOWN:
        if(f.timer>=28 && (in.lpP || in.rpP || in.ssBg || in.ssFg)){
            f.state=ST_TECH;f.timer=16;f.ssDir=in.ssBg?1:in.ssFg?-1:0;f.throwInvul=10;
            f.buf=f.bufT=0;SetMsg(f,"TECH ROLL");break;
        }
        // wake-up options: rise early, or come up swinging
        if (f.timer > 6 && in.up) { f.timer = 6; f.wakeupOpt = 1; }
        if (f.timer > 4 && (in.lkP || in.rkP)) {
            f.wakeupOpt = 2;
            StartMove(f, wantYaw, M_DRK, 6);
            break;
        }
        if (--f.timer <= 0) { f.state = ST_GETUP; f.timer = f.wakeupOpt == 1 ? 12 : 22; }
        break;

    case ST_GETUP:
        TurnToward(f, wantYaw, 0.3f);
        if (--f.timer <= 0) { f.state = ST_IDLE; f.juggle = 0; f.wakeupOpt = 0;f.throwInvul=6; }
        break;

    case ST_THROWING: {
        int t = f.moveFrame++;
        Vector3 fw = Fwd(f);
        if (o.state == ST_THROWN) {
            float k = t / 30.0f;
            o.pos = f.pos + fw * (0.55f + 0.75f * k);
            o.pos.y = sinf(k * PI) * 1.15f;
            if (t >= 30) {
                o.pos.y = 0;
                o.comboCount = 0; o.comboDmg = 0;
                DealDamage(f, o, MOVES[M_THROW].damage);
                o.state = o.ko ? ST_KO : ST_DOWN;
                o.timer = 45;
                o.kb = fw * 3.0f;
                SpawnSparks({ o.pos.x, 0.1f, o.pos.z }, { 255, 170, 60, 255 }, 18, 0.08f);
                SpawnDust(o.pos, 12, 0.04f);
                G.shake = 0.22f; G.hitstop = 9;
                G.camPunch = fmaxf(G.camPunch, 0.5f);
                Play(SND_HEAVY);
                if (o.ko) { G.slowmo = 96; Play(SND_KO); }
            }
        }
        if (t >= 48) f.state = ST_IDLE;
        break; }

    case ST_THROWN:
        // break out with a punch before the throw commits
        if (f.escapeT > 0 && (in.lpP || in.rpP)) {
            f.state = ST_IDLE;
            f.escapeT = 0;
            f.pos.y = 0;
            f.buf = f.bufT = 0;
            f.kb = dir * -5.4f;
            if (o.state == ST_THROWING) { o.state = ST_IDLE; o.kb = dir * 5.4f; }
            SetMsg(f, "BREAK");
            AddMeter(f, 8);
            SpawnSparks({ f.pos.x, 1.0f, f.pos.z }, { 200, 230, 255, 255 }, 10, 0.05f);
            G.hitstop = 6;
            Play(SND_PARRY);
        }
        break;

    case ST_KO: case ST_WIN: break;
    }

    if (f.state != ST_IDLE && f.state != ST_CROUCH && f.state != ST_DASH &&
        f.state != ST_SIDESTEP && f.state != ST_JUMP && f.state != ST_LAND && f.state!=ST_RUSH && f.state!=ST_TECH &&
        !(f.state == ST_ATTACK && f.pos.y > 0)) f.velocity = {};
    f.pos.x += (f.velocity.x + f.kb.x)*DT;
    f.pos.z += (f.velocity.z + f.kb.z)*DT;
    f.kb = f.kb * expf(-(f.pos.y > 0 ? 0.7f : 13.4f)*DT);
}

static void SeparateBodies(Fighter &a, Fighter &b) {
    if (a.state == ST_THROWN || b.state == ST_THROWN) return;
    if (fabsf(a.pos.y-b.pos.y)>1.1f) return;
    Vector3 rel = { b.pos.x - a.pos.x, 0, b.pos.z - a.pos.z };
    float d = VLen(rel);
    if (d >= BODY_DIST) return;
    Vector3 n = d > 0.001f ? rel * (1 / d) : Fwd(a);
    float push = (BODY_DIST - d) * 0.5f;
    a.pos = a.pos - n * push;
    b.pos = b.pos + n * push;
    float closing = VDot(b.kb-a.kb,n);
    if (closing < 0) { // equal masses, inelastic body contact
        Vector3 impulse = n*(-closing*0.5f);
        a.kb = a.kb-impulse; b.kb = b.kb+impulse;
    }
}

static void ClampArena(Fighter &f) {
    float r = sqrtf(f.pos.x * f.pos.x + f.pos.z * f.pos.z);
    if (r <= ARENA_R) return;
    f.pos.x *= ARENA_R / r;
    f.pos.z *= ARENA_R / r;
    // driven into the ring edge while reeling: stagger, and eat a little extra
    float speed = sqrtf(f.kb.x * f.kb.x + f.kb.z * f.kb.z);
    Vector3 normal{f.pos.x/ARENA_R,0,f.pos.z/ARENA_R};
    float outward=VDot(f.kb,normal);
    if(outward>0) f.kb=f.kb-normal*(outward*1.15f);
    float drive = VDot(f.velocity,normal);
    if (drive > 0) f.velocity = f.velocity-normal*drive;
    if ((f.state == ST_HIT || f.state == ST_LAUNCH) && outward > 4.2f && !f.ko && !f.wallSplat) {
        f.wallSplat = true;
        if (f.pos.y <= 0.08f) { f.state = ST_STAGGER; f.pos.y = 0; f.vy = 0; }
        else { f.state = ST_LAUNCH; f.vy = fminf(f.vy, 1.5f); }
        f.timer = 26;
        f.kb = { 0, 0, 0 };
        SpawnDust(f.pos, 14, 0.05f);
        SpawnSparks({ f.pos.x, 1.0f, f.pos.z }, { 255, 200, 120, 255 }, 10, 0.05f);
        G.shake = fmaxf(G.shake, 0.16f);
        SetMsg(f, "RING EDGE");
        Play(SND_THUD);
    }
}

// -----------------------------------------------------------------------------
// CPU opponent
// -----------------------------------------------------------------------------
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
        case M_FLK: in.fwd = true; in.lk = in.lkP = true; break;
        case M_BRP: in.back = true; in.rp = in.rpP = true; break;
        case M_UFRK: in.up = true; in.rk = in.rkP = true; break;
        case M_THROW: in.lp = in.lk = in.lpP = in.lkP = true; break;
        case M_WAVE: in.specialP=true;break;
        case M_RISING: in.down=true;in.specialP=true;break;
        case M_SPIN: in.back=true;in.specialP=true;break;
        case M_TORNADO:in.back=true;in.lk=in.lkP=true;break;
        case M_HEAT_BURST:case M_HEAT_SMASH:in.heatP=true;break;
        case M_IMPACT:case M_REVERSAL:in.impactP=true;break;
        case M_RAGE: in.lp = in.rp = in.lpP = in.rpP = true; break;
        default: break;
    }
}

Input ThinkAI(AIState &ai, const Fighter &me, const Fighter &op, int level) {
    static const float blockP[3] = { 0.28f, 0.55f, 0.82f };
    static const float lowP[3]   = { 0.12f, 0.33f, 0.62f };
    static const float breakP[3] = { 0.10f, 0.35f, 0.65f };
    static const int   thinkB[3] = { 42, 26, 14 };
    Input in;
    float dx = op.pos.x - me.pos.x, dz = op.pos.z - me.pos.z;
    float dist = sqrtf(dx * dx + dz * dz);
    bool canAct = me.state == ST_IDLE || me.state == ST_CROUCH;

    if(me.state==ST_PARRY){in.parry=ai.guardT-- > 0;return in;}
    if(me.state==ST_BLOCK && me.drive>=2.0f && dist<1.8f && me.timer>10 && Frand()<0.015f*(level+1)) {in.impactP=true;return in;}
    if(me.state==ST_LAUNCH && me.vy<0 && me.pos.y<0.5f && !me.ko && Frand()<0.20f*(level+1)){in.ssBg=true;return in;}
    if(me.state==ST_ATTACK && me.moveHit && me.contact==1 && IsCancelableNormal(me.move) && Frand()<0.16f*(level+1)){
        if(me.drive>3.0f && Frand()<0.25f)in.rushP=true;
        else AIPress(in,op.pos.y>0.2f?M_RISING:Frand()<0.5f?M_WAVE:M_SPIN);
        return in;
    }
    if(canAct)for(const auto &ball:G.projectiles)if(ball.owner!=me.id && VLen(ball.pos-me.pos)<3.1f && Frand()<0.12f*(level+1)){
        if(me.drive>1.0f && level>0){in.parry=true;ai.guardT=22;}
        else in.ssBg=true;
        return in;
    }
    if(canAct && op.pos.y>0.35f && dist<1.6f && Frand()<0.12f*(level+1)){AIPress(in,M_RISING);return in;}

    // being thrown: try to break out
    if (me.state == ST_THROWN && me.escapeT > 0) {
        if (Frand() < breakP[level] * 0.25f) in.lp = in.lpP = true;
        return in;
    }
    // on the floor: mix up the wake-up
    if (me.state == ST_DOWN && me.timer > 8) {
        float r = Frand();
        if (r < 0.30f) in.up = true;
        else if (r < 0.30f + 0.12f * (level + 1) && dist < 1.6f) in.rk = in.rkP = true;
        return in;
    }

    if (ai.qBtn && --ai.qDelay <= 0) { AIPress(in, ai.qBtn); ai.qBtn = 0; return in; }

    // react to an incoming attack once per attack
    if (op.state == ST_ATTACK && op.serial != ai.seen && op.moveFrame >= (level==2?6:level==1?10:14)) {
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
                else if (r < blockP[level] + 0.10f && canAct && !mv.homing) {
                    if (Frand() < 0.5f) in.ssBg = true; else in.ssFg = true;
                }
            }
        }
    }
    if (ai.guardT > 0) { ai.guardT--; in.back = ai.guardBack; in.down = ai.guardLow; return in; }

    if (ai.walkT > 0) { ai.walkT--; if (ai.walkDir > 0) in.fwd = true; else in.back = true; }
    if (!canAct) return in;

    // punish a whiffed attack while the opponent is still recovering
    if (op.state == ST_ATTACK) {
        const MoveDef &omv = MOVES[op.move];
        bool recovering = op.moveFrame > omv.startup + omv.active;
        int left = omv.startup + omv.active + omv.recovery - op.moveFrame;
        if (recovering && left > 8 && dist < 1.8f && Frand() < 0.20f + 0.25f * level) {
            AIPress(in, me.raging && left > 18 ? M_RAGE : (left > 16 ? M_DRP : M_LP));
            ai.think = 14;
            return in;
        }
    }
    if (op.state == ST_STAGGER && dist < 2.0f) {
        AIPress(in, me.raging ? M_RAGE : M_FRP);
        ai.think = 16;
        return in;
    }

    if (--ai.think > 0) return in;
    ai.think = thinkB[level] + RandInt(0, 20);

    // cash in rage when it will land
    if (me.raging && dist < 1.8f && Frand() < 0.10f + 0.12f * level) {
        AIPress(in, M_RAGE);
        return in;
    }

    if (op.state == ST_LAUNCH) {                       // juggle
        if (dist < 1.5f && op.pos.y < 1.5f) {
            if(!op.tornadoUsed && op.pos.y<1.15f && Frand()<0.4f){AIPress(in,M_TORNADO);return in;}
            int pick = RandInt(0, 2);
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
    if(canAct && dist<1.65f && me.heatAvailable && Frand()<0.14f){AIPress(in,M_HEAT_BURST);return in;}
    if(canAct && dist<1.8f && me.heatFrames>0 && me.heatFrames<240 && Frand()<0.25f){AIPress(in,M_HEAT_SMASH);return in;}
    if(canAct && dist<1.8f && op.state==ST_ATTACK && me.drive>1 && Frand()<0.10f*(level+1)){AIPress(in,M_IMPACT);return in;}
    if (dist > 3.4f) {
        bool wave=false;for(const auto &p:G.projectiles)wave|=p.owner==me.id;
        if(!wave && Frand()<0.55f){AIPress(in,M_WAVE);ai.think=35;return in;}
        if (Frand() < 0.4f) in.dashF = true; else { ai.walkT = 28; ai.walkDir = 1; }
        ai.think = 14;
        return in;
    }
    if (dist > 1.9f) {
        float r = Frand();
        if (r < 0.44f) { ai.walkT = 16; ai.walkDir = 1; ai.think = 10; }
        else if (r < 0.58f) AIPress(in, M_FRP);
        else if (r < 0.68f) AIPress(in, M_FLK);
        else if (r < 0.76f) AIPress(in, M_RK);
        else if (r < 0.86f) { if (Frand() < 0.5f) in.ssBg = true; else in.ssFg = true; }
        else in.dashF = true;
        return in;
    }

    // Close range. Bias the mix-up against however the player has been guarding:
    // someone who blocks low a lot starts eating highs, and the reverse.
    int lowB = op.lowBlocks, highB = op.highBlocks;
    float lowBias = 0.0f;
    if (lowB + highB > 6) lowBias = Clampf((float)(highB - lowB) / (float)(lowB + highB), -0.5f, 0.5f);
    float r = Frand() * 100;
    float shift = lowBias * 18.0f * (0.5f + 0.5f * level);   // >0 means punish low blockers with highs
    if      (r < 22 - shift) { AIPress(in, M_LP); if (Frand() < 0.7f) { ai.qBtn = M_RP; ai.qDelay = 12; } }
    else if (r < 38 - shift) AIPress(in, M_LK);
    else if (r < 52 + shift) AIPress(in, M_DLK);
    else if (r < 62 + shift) AIPress(in, M_DRK);
    else if (r < 70)         AIPress(in, M_DRP);
    else if (r < 76)         AIPress(in, M_FRP);
    else if (r < 82)         { if (dist < 1.1f) AIPress(in, M_THROW); else { ai.walkT = 6; ai.walkDir = 1; ai.think = 7; } }
    else if (r < 87)         AIPress(in, M_RK);
    else if (r < 91)         AIPress(in, M_UFRK);
    else if (r < 95)         in.dashB = true;
    else                     { ai.guardT = 20; ai.guardLow = false; ai.guardBack = true; }
    return in;
}

// -----------------------------------------------------------------------------
// match flow
// -----------------------------------------------------------------------------
void ResetFighter(Fighter &f, int id) {
    int wins = f.wins;
    Fighter n;
    n.id = id;
    n.wins = wins;
    if (id == 0) {
        n.name = "ANIME";
        n.skin = { 222, 170, 130, 255 };
        n.cloth = { 235, 235, 240, 255 };
        n.accent = { 200, 40, 40, 255 };
        n.hair = { 30, 25, 25, 255 };
        n.pos = { -1.5f, 0, 0 };
        n.yaw = 0;
    } else {
        n.name = "RITUAL";
        n.skin = { 205, 165, 140, 255 };
        n.cloth = { 35, 45, 80, 255 };
        n.accent = { 235, 180, 50, 255 };
        n.hair = { 210, 200, 180, 255 };
        n.pos = { 1.5f, 0, 0 };
        n.yaw = PI;
    }
    n.pose = BasePose(false, 0);
    f = n;
}

void StartRound() {
    ResetFighter(G.f[0], 0);
    ResetFighter(G.f[1], 1);
    G.ai[0] = AIState();
    G.ai[1] = AIState();
    G.timer = ROUND_FRAMES;
    G.phase = PH_INTRO;
    G.phaseT = 120;
    G.hitstop = 0;
    G.slowmo = 0;
    G.winner = -1;
    G.pending[0] = G.pending[1] = Input();
    G.trainingRecovery = 0;
    for (int i=0;i<2;++i) {
        G.lastFwdTap[i] = G.lastBackTap[i] = -100;
        G.wasFwd[i] = G.wasBack[i] = false;
    }
    G.projectiles.clear();
    G.parts.clear();
    G.flashes.clear();
    bool final = G.f[0].wins == ROUNDS_TO_WIN - 1 && G.f[1].wins == ROUNDS_TO_WIN - 1;
    if (final) SetBanner("FINAL ROUND", 80);
    else {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "ROUND %d", G.round);
        SetBanner(buf, 80);
    }
}

void ResetTraining() {
    StartRound();
    G.phase = PH_FIGHT;
    G.phaseT = 0;
    G.paused = false;
    G.f[0].meter = G.f[1].meter = METER_MAX;
    G.f[0].raging = G.f[1].raging = true;
    G.f[0].pos.x = -0.7f; G.f[1].pos.x = 0.7f;
    SetBanner("TRAINING", 45);
}

Input TrainingInput() {
    if (G.dummy == 3) return ThinkAI(G.ai[1], G.f[1], G.f[0], G.difficulty);
    Input in;
    in.back = G.dummy == 1 || G.dummy == 2;
    in.down = G.dummy == 2;
    return in;
}

void StartMatch() {
    G.f[0].wins = G.f[1].wins = 0;
    G.round = 1;
    G.paused = false;
    if (G.mode == 3) ResetTraining();
    else StartRound();
}

static void EndRound(int winner, const char *text) {
    G.phase = PH_ROUND_END;
    G.phaseT = 210;
    G.winner = winner;
    if (winner >= 0) G.f[winner].wins++;
    else {
        G.f[0].wins = std::min(G.f[0].wins + 1, ROUNDS_TO_WIN - 1);
        G.f[1].wins = std::min(G.f[1].wins + 1, ROUNDS_TO_WIN - 1);
    }
    SetBanner(text, 110);
}

static void UpdateCamera3D(bool orbit) {
    Fighter &a = G.f[0], &b = G.f[1];
    Vector3 axis = { b.pos.x - a.pos.x, 0, b.pos.z - a.pos.z };
    float sep = VLen(axis);
    if (sep > 0.05f) axis = axis * (1 / sep); else axis = { 1, 0, 0 };
    // The renderer uses a left-handed view: -Z keeps player 1 on screen-left.
    Vector3 perp = { axis.z, 0, -axis.x };
    G.bgDir = { -axis.z, 0, axis.x };
    if (orbit && !G.reducedMotion) G.camAngle += 0.004f;
    else G.camAngle += AngleDiff(G.camAngle, atan2f(perp.z, perp.x)) * 0.07f;
    float wantDist = Clampf(4.6f + sep * 0.8f, 5.2f, 12.0f);
    G.camDist = Lerpf(G.camDist, wantDist, 0.08f);
    float air = fmaxf(a.pos.y, b.pos.y);
    Vector3 tgt = { (a.pos.x + b.pos.x) * 0.5f, 1.0f + air * 0.3f, (a.pos.z + b.pos.z) * 0.5f };
    G.camTarget = VLerp(G.camTarget, tgt, 0.15f);
}

static void StepParticles() {
    for (auto &p : G.parts) {
        p.p = p.p + p.v;
        p.v.y -= (p.kind == 1) ? 0.0008f : 0.004f;      // dust hangs, sparks fall
        if (p.kind == 1) { p.v.x *= 0.94f; p.v.z *= 0.94f; }
        p.life -= (p.kind == 1) ? 0.025f : 0.04f;
        if (p.p.y < 0.02f) { p.p.y = 0.02f; p.v.y *= -0.4f; }
    }
    G.parts.erase(std::remove_if(G.parts.begin(), G.parts.end(),
                                 [](const Particle &p) { return p.life <= 0; }),
                  G.parts.end());
    for (auto &f : G.flashes) f.t += 0.12f;
    G.flashes.erase(std::remove_if(G.flashes.begin(), G.flashes.end(),
                                   [](const Flash &f) { return f.t >= 1; }),
                    G.flashes.end());
}

static void StepProjectiles(bool live) {
    if(!live){G.projectiles.clear();return;}
    for(auto &p:G.projectiles)--p.life;
    for(int step=0;step<3;++step) {
        for(auto &p:G.projectiles)if(p.life>0)p.pos=p.pos+p.velocity*(DT/3.0f);
        for(size_t i=0;i<G.projectiles.size();++i)for(size_t j=i+1;j<G.projectiles.size();++j){
            auto &a=G.projectiles[i];auto &b=G.projectiles[j];
            if(a.life>0 && b.life>0 && a.owner!=b.owner && VLen(a.pos-b.pos)<0.56f){
                a.life=b.life=0;SpawnSparks((a.pos+b.pos)*0.5f,{146,227,255,255},10,0.05f);Play(SND_BLOCK);
            }
        }
        for(auto &p:G.projectiles)if(p.life>0){
            Fighter &owner=G.f[p.owner],&target=G.f[1-p.owner];
            Fighter origin=owner,snapshot=target;
            origin.pos=p.pos-VNorm(p.velocity)*0.25f;origin.pos.y=0;
            origin.yaw=atan2f(-p.velocity.z,p.velocity.x);origin.state=ST_ATTACK;origin.move=M_WAVE;
            origin.enhanced=p.enhanced;origin.rushBonus=0;
            if(ResolveHit(owner,target,M_WAVE,origin,snapshot,&p))p.life=0;
            if(VLen(Vector3{p.pos.x,0,p.pos.z})>ARENA_R+1.0f)p.life=0;
        }
    }
    G.projectiles.erase(std::remove_if(G.projectiles.begin(),G.projectiles.end(),[](const Projectile &p){return p.life<=0;}),G.projectiles.end());
}

static bool StepFight(Input in0, Input in1, bool live) {
    G.shake *= 0.86f;
    G.camRoll *= 0.88f;
    G.camPunch *= 0.84f;
    G.screenFlash *= 0.80f;
    if (G.bannerT > 0) G.bannerT--;
    if (live) {
        PrepareCommand(G.f[0],in0);PrepareCommand(G.f[1],in1);
        LatchInput(G.pending[0], in0); LatchInput(G.pending[1], in1);
    }
    if (G.hitstop > 0) { G.hitstop--; StepParticles(); return false; }
    if (G.slowmo > 0) { G.slowmo--; if (G.slowmo % 3 != 0) return false; }

    if (!live) { in0 = Input(); in1 = Input(); }
    else {
        in0 = G.pending[0]; in1 = G.pending[1];
        ClearPressed(G.pending[0]); ClearPressed(G.pending[1]);
    }
    Fighter op0 = G.f[0], op1 = G.f[1];
    UpdateFighter(G.f[0], G.f[1], op1, in0);
    UpdateFighter(G.f[1], G.f[0], op0, in1);
    // Iterative contact projection keeps fighters separated at the circular wall.
    for (int i=0;i<8;++i) {
        SeparateBodies(G.f[0], G.f[1]);
        ClampArena(G.f[0]); ClampArena(G.f[1]);
    }
    Fighter before[2] = {G.f[0],G.f[1]};
    int moves[2] = {ActiveMove(before[0]),ActiveMove(before[1])};
    if (live) {
        bool throwClash = moves[0] == M_THROW && moves[1] == M_THROW &&
            CombatOverlap(AttackVolume(before[0],M_THROW),HurtVolume(before[1])) &&
            CombatOverlap(AttackVolume(before[1],M_THROW),HurtVolume(before[0]));
        if (throwClash) {
            for (auto &f : G.f) { f.state=ST_STAGGER; f.timer=12; f.moveHit=true; SetMsg(f,"THROW BREAK"); }
            G.hitstop=6; Play(SND_PARRY);
        } else {
            for (int i=0;i<2;++i) if (moves[i] && moves[i]!=M_THROW)
                ResolveHit(G.f[i],G.f[1-i],moves[i],before[i],before[1-i]);
            for (int i=0;i<2;++i) if (moves[i]==M_THROW && G.f[i].state==ST_ATTACK)
                ResolveHit(G.f[i],G.f[1-i],moves[i],before[i],before[1-i]);
        }
    }

    StepProjectiles(live);
    float t = G.time;
    for (auto &f : G.f) {
        float k = f.state == ST_ATTACK ? 0.38f : (f.state == ST_HIT || f.state == ST_BLOCK) ? 0.34f : 0.2f;
        BlendPose(f.pose, TargetPose(f, t), k);
        f.breath += (f.state == ST_IDLE || f.state == ST_CROUCH) ? 0.055f : 0.085f;
        UpdateTrail(f);
        // sweat beads off a fighter who is hurt and still working
        f.sweat = 1.0f - (float)f.hp / MAX_HP;
        if (f.sweat > 0.45f && f.state != ST_DOWN && f.state != ST_KO && Frand() < f.sweat * 0.10f) {
            Vector3 hp = LimbWorld(f, BOTH_HANDS);
            Particle q;
            q.p = { hp.x + FrandS() * 0.15f, f.pos.y + 1.30f + FrandS() * 0.12f, hp.z + FrandS() * 0.15f };
            q.v = { FrandS() * 0.004f, 0.004f, FrandS() * 0.004f };
            q.life = 0.5f;
            q.size = 0.018f;
            q.c = { 190, 215, 235, 255 };
            q.kind = 2;
            G.parts.push_back(q);
        }
        // walking kicks up a little grit
        if (f.walking && ((int)f.walkPhase) % 3 == 0 && Frand() < 0.25f) SpawnDust(f.pos, 1, 0.012f);
    }
    StepParticles();
    return true;
}

void Step(bool firstStepOfFrame, Input in0, Input in1) {
    if (!firstStepOfFrame) { ClearPressed(in0); ClearPressed(in1); }
    if (G.paused) return;
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
    case PH_FIGHT: {
        bool advanced = StepFight(in0, in1, true);
        UpdateCamera3D(false);
        if (G.mode == 3) {
            bool settled = true;
            for (const auto &f : G.f)
                settled &= f.state == ST_IDLE || f.state == ST_CROUCH || f.state == ST_KO;
            if (settled && G.hitstop == 0) ++G.trainingRecovery;
            else G.trainingRecovery = 0;
            if (G.trainingRecovery >= 90) {
                if (G.f[0].ko || G.f[1].ko) ResetTraining();
                else for (auto &f : G.f) {
                    f.hp = MAX_HP; f.hpLag = MAX_HP;
                    f.meter = METER_MAX; f.raging = true;
                    f.drive=6;f.burnout=0;f.driveWait=0;f.recoverable=0;f.heatAvailable=true;f.heatFrames=0;
                }
                G.trainingRecovery = 0;
            }
            break;
        }
        if (advanced && G.timer > 0) G.timer--;
        if (G.f[0].ko || G.f[1].ko) {
            if (G.f[0].ko && G.f[1].ko) EndRound(-1, "DOUBLE K.O.");
            else EndRound(G.f[0].ko ? 1 : 0, "K.O.");
        } else if (G.timer <= 0) {
            if (G.f[0].hp == G.f[1].hp) EndRound(-1, "DRAW");
            else EndRound(G.f[0].hp > G.f[1].hp ? 0 : 1, "TIME UP");
        }
        break;
    }
    case PH_ROUND_END:
        StepFight(in0, in1, false);
        UpdateCamera3D(false);
        if (G.winner >= 0 && G.phaseT < 140) {
            Fighter &w = G.f[G.winner];
            if (w.state == ST_IDLE || w.state == ST_CROUCH) { w.state = ST_WIN; w.crouched = false; }
        }
        if (G.phaseT == 100 && G.winner >= 0) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%s WINS", G.f[G.winner].name);
            SetBanner(buf, 95);
        }
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
