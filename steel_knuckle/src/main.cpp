// =============================================================================
//  STEEL KNUCKLE - a Tekken-style 3D fighting game.
//
//  SDL3 for window, input and audio; Diligent Engine for rendering, on its
//  OpenGL or Vulkan backend.
//
//    --demo          CPU versus CPU from the first frame
//    --shot N        write shot.png on frame N and exit
//    --vulkan        prefer the Vulkan backend
//    --opengl        use the OpenGL backend (default)
//    --nofx          start with post-processing off
//    --quality N     0 performance, 1 balanced (default), 2 cinematic
// =============================================================================
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "platform.h"
#include "render.h"
#include "audio.h"
#include "game.h"
#include "scene.h"
#include "character.h"
#include <filesystem>

namespace {

// Screenshots are written by hand rather than pulled in as a dependency; a
// bare-bones uncompressed PNG keeps the verification path self-contained.
bool WritePNG(const char *path, const unsigned char *rgba, int w, int h);

Input ReadHuman(int p) {
    Input in;
    auto down = [](SDL_Scancode key) { return P::KeyDown(key); };
    auto press = [](SDL_Scancode key) { return P::KeyPressed(key); };
    bool left = down(p == 0 ? SDL_SCANCODE_A : SDL_SCANCODE_LEFT);
    bool right = down(p == 0 ? SDL_SCANCODE_D : SDL_SCANCODE_RIGHT);
    in.up = down(p == 0 ? SDL_SCANCODE_W : SDL_SCANCODE_UP);
    in.down = down(p == 0 ? SDL_SCANCODE_S : SDL_SCANCODE_DOWN);
    in.ssBg = press(p == 0 ? SDL_SCANCODE_Q : SDL_SCANCODE_COMMA);
    in.ssFg = press(p == 0 ? SDL_SCANCODE_E : SDL_SCANCODE_PERIOD);
    SDL_Scancode keys[4] = {p == 0 ? SDL_SCANCODE_U : SDL_SCANCODE_KP_4,
        p == 0 ? SDL_SCANCODE_I : SDL_SCANCODE_KP_5,
        p == 0 ? SDL_SCANCODE_J : SDL_SCANCODE_KP_1,
        p == 0 ? SDL_SCANCODE_K : SDL_SCANCODE_KP_2};
    in.lp = down(keys[0]); in.lpP = press(keys[0]);
    in.rp = down(keys[1]); in.rpP = press(keys[1]);
    in.lk = down(keys[2]); in.lkP = press(keys[2]);
    in.rk = down(keys[3]); in.rkP = press(keys[3]);
    in.throwP = press(p == 0 ? SDL_SCANCODE_O : SDL_SCANCODE_KP_6);
    in.rageP = press(p == 0 ? SDL_SCANCODE_L : SDL_SCANCODE_KP_3);
    in.specialP=press(p==0?SDL_SCANCODE_F:SDL_SCANCODE_KP_0);
    in.heatP=press(p==0?SDL_SCANCODE_G:SDL_SCANCODE_KP_7);
    in.impactP=press(p==0?SDL_SCANCODE_H:SDL_SCANCODE_KP_8);
    in.parry=down(p==0?SDL_SCANCODE_V:SDL_SCANCODE_KP_9);
    in.rushP=press(p==0?SDL_SCANCODE_B:SDL_SCANCODE_KP_DECIMAL);
    static bool impactHeld[2]{};
    if (P::PadAvailable(p)) {
        left |= P::PadDown(p, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || P::PadAxis(p, SDL_GAMEPAD_AXIS_LEFTX) < -0.5f;
        right |= P::PadDown(p, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || P::PadAxis(p, SDL_GAMEPAD_AXIS_LEFTX) > 0.5f;
        in.up |= P::PadDown(p, SDL_GAMEPAD_BUTTON_DPAD_UP) || P::PadAxis(p, SDL_GAMEPAD_AXIS_LEFTY) < -0.5f;
        in.down |= P::PadDown(p, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || P::PadAxis(p, SDL_GAMEPAD_AXIS_LEFTY) > 0.5f;
        in.ssBg |= P::PadPressed(p, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        in.ssFg |= P::PadPressed(p, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        in.specialP |= P::PadPressed(p,SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        in.heatP |= P::PadPressed(p,SDL_GAMEPAD_BUTTON_LEFT_STICK);
        in.parry |= P::PadAxis(p,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)>0.5f;
        bool impact=P::PadAxis(p,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)>0.5f;
        in.impactP |= impact && !impactHeld[p];impactHeld[p]=impact;
        in.lp |= P::PadDown(p, SDL_GAMEPAD_BUTTON_WEST); in.lpP |= P::PadPressed(p, SDL_GAMEPAD_BUTTON_WEST);
        in.rp |= P::PadDown(p, SDL_GAMEPAD_BUTTON_NORTH); in.rpP |= P::PadPressed(p, SDL_GAMEPAD_BUTTON_NORTH);
        in.lk |= P::PadDown(p, SDL_GAMEPAD_BUTTON_SOUTH); in.lkP |= P::PadPressed(p, SDL_GAMEPAD_BUTTON_SOUTH);
        in.rk |= P::PadDown(p, SDL_GAMEPAD_BUTTON_EAST); in.rkP |= P::PadPressed(p, SDL_GAMEPAD_BUTTON_EAST);
    }
    else impactHeld[p]=false;
    // Capture chords now so releasing a button during hitstop cannot lose them.
    in.throwP |= (in.lpP && in.lk) || (in.lkP && in.lp);
    in.rageP |= (in.lpP && in.rp) || (in.rpP && in.lp);
    in.fwd = p == 0 ? right : left;
    in.back = p == 0 ? left : right;
    // Tap timing uses elapsed time, independent of render frame rate.
    int now = int(P::Time() * 60.0);
    if (in.fwd && !G.wasFwd[p]) { in.dashF = now - G.lastFwdTap[p] < 14; G.lastFwdTap[p] = now; }
    if (in.back && !G.wasBack[p]) { in.dashB = now - G.lastBackTap[p] < 14; G.lastBackTap[p] = now; }
    G.wasFwd[p] = in.fwd; G.wasBack[p] = in.back;
    return in;
}

}  // namespace

int main(int argc, char **argv) {
    int shotFrame = -1, quality = 1, preview = -1, windowW=SCREEN_W, windowH=SCREEN_H;
    bool demo = false, training = false, preferVulkan = false, postFx = true;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--size") && i+2<argc) { windowW=std::max(640,atoi(argv[++i])); windowH=std::max(360,atoi(argv[++i])); }
        else if (!strcmp(argv[i], "--demo")) demo = true;
        else if (!strcmp(argv[i], "--training")) training = true;
        else if (!strcmp(argv[i], "--vulkan")) preferVulkan = true;
        else if (!strcmp(argv[i], "--opengl")) preferVulkan = false;
        else if (!strcmp(argv[i], "--nofx")) postFx = false;
        else if (!strcmp(argv[i], "--quality") && i + 1 < argc) quality = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--preview") && i + 1 < argc) preview = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shotFrame = atoi(argv[++i]);
    }

    if (!P::Init("Steel Knuckle", windowW, windowH)) return 1;
    if (!R::Init(P::NativeWindow(), windowW, windowH, preferVulkan)) {
        P::Shutdown();
        return 1;
    }
    R::SetQuality(quality);
    std::filesystem::path assetDir = std::filesystem::path(SDL_GetBasePath()) / "assets";
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "--assets")) assetDir = argv[i + 1];
    if (!CharactersInit(assetDir.string().c_str()))
        std::fprintf(stderr, "[main] Some character assets failed to load; using primitive fallback for those fighters.\n");
    A::Init();

    ResetFighter(G.f[0], 0);
    ResetFighter(G.f[1], 1);
    if (demo) { G.mode = 2; StartMatch(); }
    if (training) { G.mode = 3; StartMatch(); }

    // Deterministic UI capture fixtures; only enabled with --shot.
    if (shotFrame >= 0 && preview >= 0) {
        if (preview == 1) { G.paused = true; G.pauseTab = 1; }
        if (preview == 2) { StartMatch(); G.phase = PH_FIGHT; G.paused = true; }
        if (preview == 3) { StartMatch(); G.f[0].wins = 2; G.f[1].wins = 1;
            G.phase = PH_MATCH_END; G.f[0].state = ST_WIN; G.f[1].state = ST_KO; G.bannerT = 0; }
    }
    if (shotFrame >= 0 && preview == 4) { G.paused = true; G.pauseTab = 2; }
    if(shotFrame>=0 && preview==5){G.paused=true;G.pauseTab=3;}
    if(shotFrame>=0 && preview==6){G.paused=true;G.pauseTab=2;G.movePage=1;}
    if(shotFrame>=0 && preview==7){G.mode=3;ResetTraining();G.bannerT=0;
        G.f[0].pos.x=-2;G.f[1].pos.x=2;G.f[0].heatAvailable=false;G.f[0].heatFrames=500;
        G.f[0].drive=3.5f;G.f[1].burnout=360;G.f[1].drive=0;
        G.projectiles.push_back({{-1.3f,PROJECTILE_HEIGHT,0},{9,0,0},0,0,100,true});}
    double prev = P::Time();
    float acc = 0;
    bool running = true;
    Input pendingHuman[2];

    while (running) {
        running = P::PumpEvents();
        if (P::Resized()) {
            int w, h;
            P::Size(w, h);
            R::Resize(w, h);
        }

        double now = P::Time();
        float dt = (float)(now - prev);
        prev = now;
        if (dt > DT * 4) dt = DT * 4;
        // Keep capture simulation independent of high-poly rendering speed.
        if (shotFrame >= 0) dt = DT;
        G.time = shotFrame >= 0 ? float(G.frame) * DT : (float)now;
        G.frame++;

        bool confirm = P::KeyPressed(SDL_SCANCODE_RETURN) || P::KeyPressed(SDL_SCANCODE_KP_ENTER) ||
                       P::PadPressed(0, SDL_GAMEPAD_BUTTON_SOUTH);
        bool pauseKey = P::KeyPressed(SDL_SCANCODE_P) || P::PadPressed(0, SDL_GAMEPAD_BUTTON_START) ||
                        P::PadPressed(1, SDL_GAMEPAD_BUTTON_START);
        bool escape = P::KeyPressed(SDL_SCANCODE_ESCAPE) ||
                      (G.paused && P::PadPressed(0,SDL_GAMEPAD_BUTTON_EAST));
        bool menuUp = P::KeyPressed(SDL_SCANCODE_UP) || P::PadPressed(0,SDL_GAMEPAD_BUTTON_DPAD_UP);
        bool menuDown = P::KeyPressed(SDL_SCANCODE_DOWN) || P::PadPressed(0,SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        bool wasPaused = G.paused;
        Phase menuPhase = G.phase;
        if (!P::Focused() && (G.phase==PH_FIGHT || G.phase==PH_INTRO) && shotFrame<0) G.paused=true;
        if (P::KeyPressed(SDL_SCANCODE_F3)) postFx = !postFx;
        if (P::KeyPressed(SDL_SCANCODE_F4)) R::SetQuality((R::Quality() + 1) % 3);
        if (P::KeyPressed(SDL_SCANCODE_F7)) G.reducedMotion = !G.reducedMotion;
        auto title = [&]() {
            G.paused=false; G.phase=PH_TITLE; G.bannerT=0;
            G.f[0].wins=G.f[1].wins=0;
            ResetFighter(G.f[0],0); ResetFighter(G.f[1],1);
            G.hitstop=G.slowmo=0; G.pending[0]=G.pending[1]=Input();
        };
        if (G.paused) {
            if(G.pauseTab==2) {
                int pageCount=(M_COUNT-2)/16+1;
                if(P::KeyPressed(SDL_SCANCODE_RIGHT) || P::PadPressed(0,SDL_GAMEPAD_BUTTON_DPAD_RIGHT))G.movePage=(G.movePage+1)%pageCount;
                if(P::KeyPressed(SDL_SCANCODE_LEFT) || P::PadPressed(0,SDL_GAMEPAD_BUTTON_DPAD_LEFT))G.movePage=(G.movePage+pageCount-1)%pageCount;
            }
            if (pauseKey || escape) G.paused=false;
            else if (P::KeyPressed(SDL_SCANCODE_BACKSPACE) || P::PadPressed(0,SDL_GAMEPAD_BUTTON_BACK)) title();
            else {
                if (P::KeyPressed(SDL_SCANCODE_TAB) || P::PadPressed(0,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
                    G.pauseTab=G.phase==PH_TITLE ? (G.pauseTab%3)+1 : (G.pauseTab+1)%4;
                if (P::PadPressed(0,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))
                    G.pauseTab=G.phase==PH_TITLE ? ((G.pauseTab+1)%3)+1 : (G.pauseTab+3)%4;
                if (G.phase==PH_TITLE && confirm) StartMatch();
                else if (G.pauseTab==0) {
                    if(menuUp)G.menuSelection=(G.menuSelection+4)%5;
                    if(menuDown)G.menuSelection=(G.menuSelection+1)%5;
                    if(confirm) {
                        switch(G.menuSelection) {
                            case 0:G.paused=false;break;
                            case 1:G.pauseTab=1;break;
                            case 2:G.pauseTab=2;break;
                            case 3:StartMatch();break;
                            case 4:title();break;
                        }
                    }
                }
            }
        } else if (G.phase == PH_TITLE) {
            if (pauseKey) {G.paused=true;G.pauseTab=1;}
            if (P::KeyPressed(SDL_SCANCODE_F1) || menuDown) G.mode=(G.mode+1)%4;
            if (menuUp) G.mode=(G.mode+3)%4;
            if (P::KeyPressed(SDL_SCANCODE_F2) || P::KeyPressed(SDL_SCANCODE_RIGHT) ||
                P::PadPressed(0,SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) G.difficulty=(G.difficulty+1)%3;
            if (P::KeyPressed(SDL_SCANCODE_LEFT) || P::PadPressed(0,SDL_GAMEPAD_BUTTON_DPAD_LEFT)) G.difficulty=(G.difficulty+2)%3;
            if (confirm) StartMatch();
            if (escape) running=false;
        } else if (G.phase==PH_MATCH_END) {
            if(confirm)StartMatch();
            if(P::KeyPressed(SDL_SCANCODE_BACKSPACE) || P::PadPressed(0,SDL_GAMEPAD_BUTTON_BACK) || escape)title();
        } else {
            if(pauseKey || escape){G.paused=true;G.pauseTab=0;G.menuSelection=0;}
            if(G.mode==3 && !G.paused) {
                if(P::KeyPressed(SDL_SCANCODE_F6))G.showHitboxes=!G.showHitboxes;
                if(P::KeyPressed(SDL_SCANCODE_F5)){G.dummy=(G.dummy+1)%4;G.f[1].guardHeld=true;}
                if(P::KeyPressed(SDL_SCANCODE_R) || P::PadPressed(0,SDL_GAMEPAD_BUTTON_BACK))ResetTraining();
            }
        }
        bool menuTransition=wasPaused!=G.paused || menuPhase!=G.phase;
        if(menuTransition) {
            pendingHuman[0]=pendingHuman[1]=Input();G.pending[0]=G.pending[1]=Input();
            for(auto &f:G.f){f.buf=f.bufT=0;}
            acc=0;
        }

        LatchInput(pendingHuman[0], ReadHuman(0));
        LatchInput(pendingHuman[1], ReadHuman(1));
        if (G.paused || G.phase != PH_FIGHT || menuTransition) {
            ClearPressed(pendingHuman[0]); ClearPressed(pendingHuman[1]);
        }
        acc = fminf(acc + dt, DT * 4);
        bool first = true;
        while (acc >= DT) {
            Input in0 = pendingHuman[0], in1 = pendingHuman[1];
            bool thinking = G.phase == PH_FIGHT && !G.paused && G.hitstop == 0 && G.slowmo == 0;
            if (G.mode == 2) in0 = thinking ? ThinkAI(G.ai[0], G.f[0], G.f[1], G.difficulty) : Input();
            if (G.mode == 3) in1 = thinking ? TrainingInput() : Input();
            else if (G.mode != 1) in1 = thinking ? ThinkAI(G.ai[1], G.f[1], G.f[0], G.difficulty) : Input();
            bool aiFirst0 = first || G.mode == 2, aiFirst1 = first || G.mode != 1;
            if (!aiFirst0) ClearPressed(in0);
            if (!aiFirst1) ClearPressed(in1);
            Step(true, in0, in1);
            ClearPressed(pendingHuman[0]); ClearPressed(pendingHuman[1]);
            first = false;
            acc -= DT;
        }

        bool capturing = shotFrame >= 0 && G.frame >= shotFrame;
        if (capturing) R::RequestCapture();

        SceneDrawFrame(G.time, postFx);

        if (capturing) {
            int w = 0, h = 0;
            std::vector<unsigned char> pixels;
            if (R::TakeCapture(pixels, w, h) && WritePNG("shot.png", pixels.data(), w, h))
                std::printf("[main] wrote shot.png (%dx%d)\n", w, h);
            else
                std::printf("[main] screenshot failed\n");
            running = false;
        }
    }

    CharactersShutdown();
    A::Shutdown();
    R::Shutdown();
    P::Shutdown();
    return 0;
}

// -----------------------------------------------------------------------------
// Minimal PNG writer: zlib stored (uncompressed) blocks, so there is no
// compression library to pull in just to verify a frame.
// -----------------------------------------------------------------------------
namespace {

unsigned int Crc32(const unsigned char *data, size_t len, unsigned int crc = 0) {
    static unsigned int table[256];
    static bool built = false;
    if (!built) {
        for (unsigned int i = 0; i < 256; i++) {
            unsigned int c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        built = true;
    }
    crc = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void PushU32(std::vector<unsigned char> &v, unsigned int x) {
    v.push_back((unsigned char)(x >> 24));
    v.push_back((unsigned char)(x >> 16));
    v.push_back((unsigned char)(x >> 8));
    v.push_back((unsigned char)x);
}

void PushChunk(std::vector<unsigned char> &out, const char *type, const std::vector<unsigned char> &data) {
    PushU32(out, (unsigned int)data.size());
    std::vector<unsigned char> body;
    body.insert(body.end(), type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    PushU32(out, Crc32(body.data(), body.size()));
}

bool WritePNG(const char *path, const unsigned char *rgba, int w, int h) {
    // raw scanlines, each prefixed with filter byte 0
    std::vector<unsigned char> raw;
    raw.reserve((size_t)(w * 4 + 1) * h);
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + (size_t)y * w * 4, rgba + (size_t)(y + 1) * w * 4);
    }

    // zlib wrapper around stored deflate blocks
    std::vector<unsigned char> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n = raw.size() - pos;
        if (n > 65535) n = 65535;
        bool last = (pos + n) >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back((unsigned char)(n & 0xFF));
        z.push_back((unsigned char)(n >> 8));
        z.push_back((unsigned char)(~n & 0xFF));
        z.push_back((unsigned char)((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    // adler32 of the raw data
    unsigned int a = 1, b = 0;
    for (unsigned char c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    PushU32(z, (b << 16) | a);

    std::vector<unsigned char> out = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<unsigned char> ihdr;
    PushU32(ihdr, (unsigned int)w);
    PushU32(ihdr, (unsigned int)h);
    ihdr.push_back(8);      // bit depth
    ihdr.push_back(6);      // colour type RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    PushChunk(out, "IHDR", ihdr);
    PushChunk(out, "IDAT", z);
    PushChunk(out, "IEND", {});

    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    size_t written = fwrite(out.data(), 1, out.size(), fp);
    fclose(fp);
    return written == out.size();
}

}  // namespace
