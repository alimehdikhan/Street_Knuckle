// =============================================================================
//  game.h - fight simulation: move data, fighter state, match flow.
//
//  Pure logic and pure data. It knows nothing about the renderer or SDL; the
//  scene layer reads this state and draws it.
// =============================================================================
#pragma once

#include <vector>

#include "mathx.h"

// ---- tuning ------------------------------------------------------------------
const int   SCREEN_W      = 1280;
const int   SCREEN_H      = 720;
const float DT            = 1.0f / 60.0f;
const float ARENA_R       = 11.0f;
const int   MAX_HP        = 130;
const int   ROUND_FRAMES  = 60 * 60;
const int   ROUNDS_TO_WIN = 2;
const float BODY_DIST     = 0.72f;
const float GRAVITY       = 28.0f; // metres / second squared
const float JUMP_SPEED    = 7.4f;  // metres / second
const int   METER_MAX     = 100;
const int   TRAIL_LEN     = 14;

// ---- move data ---------------------------------------------------------------
enum Level  { LV_HIGH, LV_MID, LV_LOW, LV_THROW, LV_SPECIAL };
enum Effect { FX_NORMAL, FX_KNOCKDOWN, FX_LAUNCH, FX_SWEEP, FX_WALLSPLAT, FX_TORNADO };
enum Limb   { L_HAND, R_HAND, L_FOOT, R_FOOT, BOTH_HANDS };

enum MoveId {
    M_NONE = 0,
    M_LP, M_RP, M_LK, M_RK,
    M_DLP, M_DRP, M_DLK, M_DRK,
    M_FRP,      // f+RP  power straight
    M_FLK,      // f+LK  advancing mid kick, safe poke
    M_BRP,      // b+RP  spinning backfist, high, knocks down
    M_UFRK,     // u+RK  hopkick, mid launcher
    M_WRRP,     // while rising RP, mid launcher out of crouch
    M_THROW,
    M_RAGE,     // LP+RP at full meter
    M_AIR_KICK, // kick while airborne
    M_WAVE, M_RISING, M_SPIN, M_TORNADO,
    M_HEAT_BURST, M_HEAT_SMASH, M_IMPACT, M_REVERSAL,
    M_COUNT
};

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
    bool  homing;                 // cannot be sidestepped
    bool  armored;                // absorbs one hit during startup
};

extern const MoveDef MOVES[M_COUNT];

// ---- input -------------------------------------------------------------------
struct Input {
    bool fwd = false, back = false, up = false, down = false;
    bool dashF = false, dashB = false;
    bool ssBg = false, ssFg = false;                          // sidestep back / front
    bool lp = false, rp = false, lk = false, rk = false;       // held
    bool throwP = false, rageP = false;
    bool specialP = false, heatP = false, impactP = false, rushP = false, parry = false;
    int motion = 0; // 1 quarter-circle forward, 2 dragon-punch, 3 quarter-circle back
    bool enhanced = false;
    bool lpP = false, rpP = false, lkP = false, rkP = false;   // pressed this frame
    bool commandCaptured = false;
    unsigned commandDirections = 0; // directions at the attack edge, not at hitstop exit
};

// ---- pose --------------------------------------------------------------------
struct Pose {
    float hipY = 0.92f, lean = 0.12f, tilt = 0.0f;
    Vector3 lHand = { 0.34f, -0.02f, 0.08f }, rHand = { 0.22f, -0.12f, -0.06f };
    Vector3 lFoot = { 0.24f, -0.81f, -0.04f }, rFoot = { -0.24f, -0.81f, 0.06f };
};

enum State {
    ST_IDLE, ST_CROUCH, ST_DASH, ST_SIDESTEP, ST_JUMP, ST_ATTACK, ST_HIT, ST_BLOCK,
    ST_LAUNCH, ST_DOWN, ST_GETUP, ST_THROWING, ST_THROWN, ST_KO, ST_WIN, ST_STAGGER, ST_LAND, ST_PARRY, ST_RUSH, ST_TECH, ST_PREJUMP
};

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
    Vector3 pos{}, kb{}, velocity{}; // velocity is locomotion; kb is impact momentum
    float vy = 0, yaw = 0;
    State state = ST_IDLE;
    int timer = 0;
    bool crouched = false;
    int move = 0, moveFrame = 0, serial = 0;
    bool moveHit = false;
    int contact = 0, frameAdvantage = 0; // 1 hit, 2 block, 3 parry, 4 armor
    bool wallSplat = false;
    int buf = 0, bufT = 0;
    bool bufEnhanced = false, enhanced = false;
    float drive = 6.0f;
    int driveWait = 0, burnout = 0;
    bool heatAvailable = true, tornadoUsed = false;
    int heatFrames = 0, recoverable = 0, armorHits = 0;
    int rushBonus = 0, rushReady = 0, perfectScale = 0;
    int parryPerfect = 0, throwInvul = 0, techWindow = 0;
    int lowParryWindow = 0, lowParryCooldown = 0;
    bool downForwardHeld = false;
    int inputClock = 0, historyCount = 0, historyDir[16]{}, historyTime[16]{};
    int hp = MAX_HP;
    float hpLag = MAX_HP;
    int juggle = 0;
    int comboCount = 0, comboDmg = 0;
    int showCount = 0, showDmg = 0, showT = 0;
    int ssDir = 0, dashDir = 0;
    float jumpVx = 0;
    bool ko = false, walking = false;
    int walkSign = 1;
    int wins = 0;
    float flash = 0, walkPhase = 0;
    const char *msg = ""; int msgT = 0;
    Input in;
    Pose pose;

    Trail trail;
    int meter = 0;                  // rage, fills from damage dealt and taken
    bool raging = false;
    int rageFlash = 0;
    int parryCooldown = 0;
    int parryWindow = 0;            // frames left in which a block counts as a parry
    int parryFreeze = 0;            // successful parry: attacker is held, defender acts
    bool guardHeld = false;         // previous frame's guard, to detect a fresh block
    int escapeT = 0;                // window to break a throw with LP
    bool armorUsed = false;
    int lowBlocks = 0, highBlocks = 0;   // guard habits the AI reads
    float breath = 0;
    float sweat = 0;
    int wakeupOpt = 0;              // 0 normal, 1 quick rise, 2 wake-up kick
};

struct AIState {
    int think = 30, seen = -1, guardT = 0;
    bool guardLow = false, guardBack = true;
    int qBtn = 0, qDelay = 0;
    int walkT = 0, walkDir = 0;
    int whiffPunish = 0;            // counts down while the opponent is recovering
    int readLow = 0;                // adapts to how the player guards
};

struct Particle {
    Vector3 p, v;
    float life, size;
    Color c;
    int kind;                       // 0 spark, 1 dust, 2 sweat, 3 ember
};
struct Flash {
    Vector3 p;
    float t;
    Color c;
};

struct Projectile {
    Vector3 pos{}, velocity{};
    int owner = 0, serial = 0, life = 100;
    bool enhanced = false;
};

enum Phase { PH_TITLE, PH_INTRO, PH_FIGHT, PH_ROUND_END, PH_MATCH_END };

struct Game {
    Phase phase = PH_TITLE;
    int mode = 0;            // 0 CPU, 1 local versus, 2 demo, 3 training
    int dummy = 0;           // 0 standing, 1 high guard, 2 low guard, 3 sparring
    int trainingRecovery = 0;
    bool showHitboxes = false;
    Input pending[2];        // edges survive hitstop and slow motion
    int difficulty = 1;
    int round = 1, timer = ROUND_FRAMES, phaseT = 0;
    int hitstop = 0, slowmo = 0;
    float shake = 0;
    bool paused = false;
    bool reducedMotion = false;
    int movePage = 0;
    int pauseTab = 0; // 0 resume menu, 1 controls, 2 moves
    int menuSelection = 0;
    char banner[48] = ""; int bannerT = 0;
    float camAngle = -PI * 0.5f, camDist = 7.0f;
    Vector3 camTarget = { 0, 1, 0 };
    Vector3 bgDir = { 0, 0, 1 };
    float camRoll = 0, camPunch = 0;
    float screenFlash = 0;
    Vector3 flashCol = { 1.0f, 0.85f, 0.6f };
    std::vector<Projectile> projectiles;
    std::vector<Particle> parts;
    std::vector<Flash> flashes;
    long frame = 0;
    int winner = -1;
    Fighter f[2];
    AIState ai[2];
    int lastFwdTap[2] = { -100, -100 }, lastBackTap[2] = { -100, -100 };
    bool wasFwd[2] = { false, false }, wasBack[2] = { false, false };
    float time = 0;          // seconds, drives idle animation and flicker
};

extern Game G;

// ---- simulation --------------------------------------------------------------
void ResetFighter(Fighter &f, int id);
void StartMatch();
void StartRound();
void Step(bool firstStepOfFrame, Input in0, Input in1);
Input ThinkAI(AIState &ai, const Fighter &me, const Fighter &op, int level);
void ClearPressed(Input &in);
void LatchInput(Input &pending, const Input &fresh);
Input TrainingInput();
void ResetTraining();
void PrepareCommand(Fighter &f, Input &in);
bool IsSpecial(int move);
bool IsCancelableNormal(int move);

// Body-space helpers shared by the renderer and the trail sampler.
Matrix FighterRoot(const Fighter &f);
Vector3 LimbWorld(const Fighter &f, Limb limb);

// Oriented combat boxes in world space, shared by simulation and training view.
struct CombatVolume { Vector3 center, half; float yaw; };
CombatVolume HurtVolume(const Fighter &f);
CombatVolume AttackVolume(const Fighter &f, int move);
bool CombatOverlap(const CombatVolume &a, const CombatVolume &b);
