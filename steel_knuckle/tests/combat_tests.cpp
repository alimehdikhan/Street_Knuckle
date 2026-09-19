#include "game.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
namespace A { void Play(SoundId) {} }
static int checks=0;
static void check(bool ok,const char *name) { ++checks; if(!ok) { std::fprintf(stderr,"FAIL: %s\n",name); std::exit(1); } }
static void setup(float distance=0.78f) {
    G=Game(); G.mode=1; StartMatch(); G.phase=PH_FIGHT; G.bannerT=0;
    G.f[0].pos={-distance/2,0,0}; G.f[1].pos={distance/2,0,0};
}
static void tick(Input a={},Input b={}) { G.time+=DT; ++G.frame; Step(true,a,b); }
static void frames(int n,Input a={},Input b={}) { for(int i=0;i<n;++i) tick(a,b); }
static Input punch() { Input i; i.lp=i.lpP=true; return i; }
static Input kick() { Input i; i.lk=i.lkP=true; return i; }
static Input direction(int n) {
    Input in;in.fwd=n==3 || n==6 || n==9;in.back=n==1 || n==4 || n==7;
    in.down=n>=1 && n<=3;in.up=n>=7 && n<=9;return in;
}
static void command(int a,int b,int c,bool kickButton=false,bool ex=false,int player=0) {
    Input end=direction(c);
    if(kickButton){end.lk=end.lkP=true;if(ex)end.rk=end.rkP=true;}
    else {end.lp=end.lpP=true;if(ex)end.rp=end.rpP=true;}
    if(player==0){tick(direction(a));tick(direction(b));tick(end);}
    else {tick({},direction(a));tick({},direction(b));tick({},end);}
}
static void activeStrike(int move) {
    G.f[0].state=ST_ATTACK;G.f[0].move=move;G.f[0].moveFrame=MOVES[move].startup;G.f[0].moveHit=false;
}
int main() {
    setup(); tick(punch()); frames(11); check(G.f[1].hp<MAX_HP,"jab connects in range");
    int hp=G.f[1].hp; frames(9); check(G.f[1].hp==hp,"one hit per attack");
    setup(4); tick(punch()); frames(35); check(G.f[1].hp==MAX_HP,"jab whiffs at distance");
    setup(); Input guard;guard.back=true;G.f[1].guardHeld=true;tick(punch(),guard);frames(13,{},guard);
    check(G.f[1].hp==MAX_HP && G.f[1].state==ST_BLOCK,"standing guard blocks high");
    setup(); Input crouch;crouch.down=true; tick(punch(),crouch);frames(16,{},crouch);
    check(G.f[1].hp==MAX_HP,"crouch ducks high attacks");
    setup(); guard.down=true;G.f[1].guardHeld=true;tick(kick(),guard);frames(18,{},guard);
    check(G.f[1].hp<MAX_HP,"mid beats low guard");
    setup(); Input low=kick(); low.down=true;guard.down=false;G.f[1].guardHeld=true;tick(low,guard);frames(21,{},guard);
    check(G.f[1].hp<MAX_HP,"low beats standing guard");
    setup();guard.down=true;G.f[1].guardHeld=true;tick(low,guard);frames(21,{},guard);
    check(G.f[1].hp==MAX_HP,"low guard blocks lows");
    setup();tick(punch());frames(9);Input timedParry;timedParry.parry=true;frames(2,{},timedParry);
    check(G.f[0].state==ST_STAGGER,"two-frame Drive Parry staggers a melee attacker");
    setup();tick(punch());frames(11);check(G.hitstop>0,"hitstop starts on contact");
    Input straight;straight.rp=straight.rpP=true;tick(straight);frames(13);
    check(G.f[0].move==M_RP,"combo input survives hitstop");
    setup(4);tick(punch());frames(11);tick(straight);frames(3);
    check(G.f[0].move==M_LP,"whiff cannot cancel into a string");
    setup();Input grab;grab.throwP=true;tick(grab);frames(13);
    check(G.f[1].state==ST_THROWN,"dedicated throw connects");tick({},punch());
    check(G.f[1].state==ST_IDLE && G.f[1].pos.y==0,"throw escape grounds defender");
    setup();tick(grab);frames(55);check(G.f[1].hp==MAX_HP-MOVES[M_THROW].damage,"throw damage applies once");
    setup();Input jump;jump.up=true;tick(jump);frames(4);tick(kick());
    check(G.f[0].move==M_AIR_KICK,"jump kick starts in air");frames(65);
    check(G.f[0].pos.y==0 && G.f[0].state==ST_IDLE,"air attack lands and recovers");
    setup();G.f[0].raging=true;G.f[0].meter=METER_MAX;Input rage;rage.rageP=true;tick(rage);
    check(G.f[0].move==M_RAGE && G.f[0].meter==0,"rage spends full meter");
    setup();G.paused=true;int timer=G.timer;tick(punch());check(G.timer==timer && G.f[0].state==ST_IDLE,"pause freezes simulation");
    setup();G.timer=1;G.f[0].hp=100;G.f[1].hp=50;tick();check(G.phase==PH_ROUND_END && G.winner==0,"timeout awards higher health");
    setup();G.f[1].hp=1;tick(punch());frames(12);check(G.phase==PH_ROUND_END && G.f[0].wins==1,"KO awards round");
    frames(230);check(G.round==2 && G.phase==PH_INTRO && G.f[1].hp==MAX_HP,"next round resets health");
    setup();G.f[0].wins=1;G.f[1].hp=1;tick(punch());frames(250);
    check(G.phase==PH_MATCH_END,"first to two wins match");
    G=Game();G.mode=3;StartMatch();check(G.phase==PH_FIGHT && G.f[0].raging,"training starts ready");
    G.dummy=1;float x=G.f[1].pos.x;for(int i=0;i<60;++i)tick({},TrainingInput());
    check(std::fabs(G.f[1].pos.x-x)<0.001f,"guard dummy holds position");
    G.f[1].hp=25;frames(95);check(G.f[1].hp==MAX_HP && G.timer==ROUND_FRAMES,"training refills health with unlimited time");
    Input pending;LatchInput(pending,punch());LatchInput(pending,Input());
    check(pending.lpP && !pending.lp,"input latch retains edge with latest held state");
    ClearPressed(pending);check(!pending.lpP,"input edge consumed once");
    setup(2); Input jumpForward; jumpForward.up=jumpForward.fwd=true; tick(jumpForward);
    float peak=0;
    for(int i=0;i<100;++i){tick();peak=std::fmax(peak,G.f[0].pos.y);check(G.f[0].pos.y>=0,"no ground penetration");}
    check(peak>0.8f && peak<1.3f && G.f[0].pos.y==0,"ballistic jump has bounded apex and lands");
    setup(0.1f);tick();check(VLen(G.f[1].pos-G.f[0].pos)>=BODY_DIST-0.001f,"body penetration separates");
    setup();G.f[0].pos={-ARENA_R,0,0};G.f[0].kb={-8,0,0};tick();
    check(VLen(G.f[0].pos)<=ARENA_R+0.001f && G.f[0].kb.x>=0,"wall contains fighter and reflects outward impulse");
    setup();G.f[0].kb={3,0,0};frames(30);check(VLen(G.f[0].kb)<0.02f,"ground friction settles knockback");
    setup();auto attack=AttackVolume(G.f[0],M_LP);
    check(CombatOverlap(attack,HurtVolume(G.f[1])),"active strike overlaps standing hurtbox");
    G.f[1].pos.y=2.0f;check(!CombatOverlap(attack,HurtVolume(G.f[1])),"height separates airborne hurtbox");
    G.f[1].pos.y=0;G.f[1].pos.z=2;check(!CombatOverlap(attack,HurtVolume(G.f[1])),"sidestep outside attack volume whiffs");
    setup();G.f[1].crouched=true;check(!CombatOverlap(AttackVolume(G.f[0],M_LP),HurtVolume(G.f[1])),"high hitbox clears crouching body");

    setup();tick(punch(),punch());frames(11);
    check(G.f[0].hp==MAX_HP-6 && G.f[1].hp==MAX_HP-6,"simultaneous jabs trade equal damage");
    check(G.f[0].state==ST_HIT && G.f[1].state==ST_HIT,"both trading fighters enter hit stun");
    setup();G.f[0].hp=G.f[1].hp=1;tick(punch(),punch());frames(11);
    check(G.f[0].ko && G.f[1].ko && G.winner==-1,"simultaneous lethal hits produce double KO");
    setup();tick(grab,grab);frames(13);
    check(G.f[0].hp==MAX_HP && G.f[1].hp==MAX_HP &&
          G.f[0].state==ST_STAGGER && G.f[1].state==ST_STAGGER,"simultaneous throws break symmetrically");
    setup();G.hitstop=5;Input directional=straight;directional.down=true;
    tick(directional);frames(5);
    check(G.f[0].move==M_DRP,"directional command survives hitstop after releasing down");
    setup();G.hitstop=5;tick(punch());tick(kick());frames(4);
    check(G.f[0].move==M_LK,"latest buffered attack replaces old edge without phantom throw");
    setup(5);Input forward;forward.fwd=true;tick(forward);
    float firstSpeed=VLen(G.f[0].velocity);frames(8,forward);
    check(firstSpeed>0 && firstSpeed<2.88f && std::fabs(VLen(G.f[0].velocity)-2.88f)<0.01f,"walk accelerates to stable speed");
    float stopX=G.f[0].pos.x;frames(8);
    check(VLen(G.f[0].velocity)<0.001f && G.f[0].pos.x-stopX<0.12f,"walk braking stops without skating");
    setup(5);tick(jumpForward);frames(40);
    check(G.f[0].pos.y==0 && G.f[0].vy==0,"landing clears vertical momentum");
    setup();G.phase=PH_INTRO;G.phaseT=77;G.paused=true;frames(20);
    check(G.phaseT==77 && G.f[0].pos.x==-0.39f,"pause freezes round intro too");
    setup();G.f[0].pos={ARENA_R,0,0};G.f[1].pos={ARENA_R-0.05f,0,0};tick();
    check(VLen(G.f[0].pos)<=ARENA_R+0.001f && VLen(G.f[1].pos)<=ARENA_R+0.001f,"corner solver keeps both fighters inside arena");
    check(VLen(G.f[0].pos-G.f[1].pos)>BODY_DIST-0.005f,"corner solver does not leave overlapping bodies");
    setup(4);G.f[0].pos={ARENA_R-0.01f,0.8f,0};G.f[0].state=ST_LAUNCH;G.f[0].kb={8,0,0};G.f[0].vy=3;tick();
    check(G.f[0].wallSplat && G.f[0].pos.y>0.7f,"airborne wall impact never teleports fighter to ground");
    setup();G.f[1].state=ST_LAUNCH;G.f[1].pos.y=0.7f;G.f[1].vy=0;G.f[1].juggle=5;
    G.f[0].state=ST_ATTACK;G.f[0].move=M_LP;G.f[0].moveFrame=MOVES[M_LP].startup;tick();
    check(G.f[1].hp<MAX_HP && G.f[1].vy<0,"late juggle hit cannot reset upward velocity");
    setup();G.f[1].state=ST_HIT;G.f[1].timer=30;G.f[1].comboCount=4;
    G.f[0].state=ST_ATTACK;G.f[0].move=M_LP;G.f[0].moveFrame=MOVES[M_LP].startup;tick();
    check(MAX_HP-G.f[1].hp<MOVES[M_LP].damage,"grounded combos receive damage scaling");
    setup();Input parryHold;parryHold.parry=true;frames(4,{},parryHold);
    check(G.f[1].parryPerfect==0 && G.f[1].state==ST_PARRY,"holding parry cannot extend its perfect window");

    setup();tick(punch());int chain=0;
    for(int n=0;n<130;++n){
        Input next;
        if(G.f[0].moveHit){
            if(chain==0 && G.f[0].move==M_LP){next=straight;++chain;}
            else if(chain==1 && G.f[0].move==M_RP){next=kick();++chain;}
            else if(chain==2 && G.f[0].move==M_LK){next.rk=next.rkP=true;++chain;}
        }
        tick(next);
    }
    check(chain==3 && G.f[0].showCount==4,"confirmed four-hit string connects through its finisher");
    check(G.f[0].showDmg<46,"complete string applies progressive combo scaling");
    setup();G.hitstop=2;int freezeTimer=G.timer;frames(2);
    check(G.timer==freezeTimer,"round clock stays frozen for every hitstop tick");

    setup(6);command(2,3,6);check(G.f[0].move==M_WAVE,"quarter-circle forward plus punch produces a wave");
    frames(16);check(G.projectiles.size()==1 && G.projectiles[0].owner==0,"wave emits one travelling projectile");
    frames(50);check(G.f[1].hp<MAX_HP && G.projectiles.empty(),"projectile damages once and is consumed");
    setup(6);command(2,3,6,false,false,1);frames(16);
    check(G.projectiles.size()==1 && G.projectiles[0].velocity.x<0,"P2 motion commands fire toward P1");
    setup(6);command(6,2,3);check(G.f[0].move==M_RISING,"dragon-punch motion has priority over command normals");
    setup(6);command(2,1,4,true);check(G.f[0].move==M_SPIN,"quarter-circle back plus kick starts cyclone");
    setup(6);command(2,3,6,false,true);
    check(G.f[0].move==M_WAVE && G.f[0].enhanced && std::fabs(G.f[0].drive-4)<0.01f,"Overdrive spends exactly two Drive stocks");
    setup(6);tick(direction(2));tick(direction(3));frames(24);Input stale=punch();stale.fwd=true;tick(stale);
    check(G.f[0].move==M_LP,"expired motion history cannot trigger a special");
    setup(6);G.hitstop=5;command(2,3,6);frames(3);
    check(G.f[0].move==M_WAVE,"motion entered inside hitstop survives to the first live tick");
    setup(6);tick(direction(2));Input shortcut;shortcut.fwd=true;shortcut.lp=shortcut.lpP=true;tick(shortcut);
    check(G.f[0].move==M_LP,"missing diagonal does not accidentally produce a quarter-circle");
    setup();tick(punch());frames(11);Input special;special.specialP=true;tick(special);frames(8);
    check(G.f[0].move==M_WAVE,"confirmed normal cancels into special before normal recovery ends");
    setup(6);tick(punch());frames(11);tick(special);frames(3);
    check(G.f[0].move==M_LP,"whiffed normal cannot special-cancel");
    setup();Input rush;rush.rushP=true;tick(punch());frames(11);tick(rush);frames(6);
    check(G.f[0].state==ST_RUSH && G.f[0].drive<3.01f,"contact cancel Drive Rush spends three stocks");
    setup(6);tick(rush);check(G.f[0].state==ST_RUSH && std::fabs(G.f[0].drive-5)<0.01f,"raw Drive Rush spends one stock");
    frames(4);tick(punch());check(G.f[0].state==ST_ATTACK && G.f[0].rushBonus==4,"first rush normal gains four frames of advantage");
    setup(6);G.f[0].drive=0.5f;tick(rush);check(G.f[0].state!=ST_RUSH && G.f[0].drive>=0,"insufficient Drive rejects a rush without overspending");
    setup(6);Input impact;impact.impactP=true;G.f[0].drive=1;tick(impact);
    check(G.f[0].move==M_IMPACT && G.f[0].burnout>0 && G.f[0].drive==0,"spending final Drive stock enters burnout");
    frames(640);check(G.f[0].burnout==0 && G.f[0].drive==6,"burnout recovers to six stocks after its timer");
    setup();G.f[1].state=ST_ATTACK;G.f[1].move=M_IMPACT;G.f[1].moveFrame=1;
    for(int n=0;n<3;++n){G.hitstop=0;activeStrike(M_LP);tick();}
    check(G.f[1].armorHits==2 && G.f[1].state==ST_HIT,"Drive Impact absorbs two hits and loses to a third");
    setup();G.f[1].state=ST_ATTACK;G.f[1].move=M_IMPACT;G.f[1].moveFrame=1;activeStrike(M_THROW);tick();
    check(G.f[1].state==ST_THROWN,"throws beat Drive Impact armor");
    setup();Input heldParry;heldParry.parry=true;frames(4,{},heldParry);activeStrike(M_DLK);tick({},heldParry);
    check(G.f[1].hp==MAX_HP && G.f[1].state==ST_PARRY,"held Drive Parry defends lows after the perfect window");
    setup();frames(4,{},heldParry);activeStrike(M_THROW);tick({},heldParry);
    check(G.f[1].state==ST_THROWN,"throws beat held Drive Parry");
    setup();frames(4,{},heldParry);tick();check(G.f[1].state==ST_PARRY_END && G.f[1].timer==16,"released parry has vulnerable recovery");
    setup();G.f[0].state=ST_BLOCK;G.f[0].timer=18;tick(impact);
    check(G.f[0].move==M_REVERSAL && G.f[0].drive<4.01f,"Drive Reversal escapes blockstun for two stocks");
    setup();G.f[1].state=ST_ATTACK;G.f[1].move=M_RISING;G.f[1].moveFrame=1;G.f[1].enhanced=true;activeStrike(M_LP);tick();
    check(G.f[1].hp==MAX_HP,"Overdrive uppercut is strike-invulnerable during startup");
    setup();G.f[1].state=ST_ATTACK;G.f[1].move=M_RISING;G.f[1].moveFrame=24;activeStrike(M_LP);tick();
    check(G.f[1].hp<MAX_HP && G.f[0].msgT>0,"blocked or missed uppercut leaves punishable recovery");
    setup(6);Input heat;heat.heatP=true;tick(heat);
    check(G.f[0].heatFrames==600 && !G.f[0].heatAvailable,"Heat Burst activates the once-per-round resource");
    frames(45);tick(heat);check(G.f[0].move==M_HEAT_SMASH && G.f[0].heatFrames==0,"Heat Smash consumes remaining Heat");
    frames(70);tick(heat);check(G.f[0].move!=M_HEAT_BURST && !G.f[0].heatAvailable,"spent Heat cannot reactivate in the same round");
    setup();activeStrike(M_FRP);tick();check(G.f[0].heatFrames==900 && G.f[0].state==ST_RUSH,"Heat engager hit grants Heat and a forward chase");
    setup();G.f[0].heatAvailable=false;G.f[0].heatFrames=500;activeStrike(M_LP);Input stableGuard;stableGuard.back=true;tick({},stableGuard);
    check(G.f[1].hp<MAX_HP && G.f[1].recoverable>0,"Heat block pressure creates recoverable health");
    setup();G.f[0].hp=90;G.f[0].recoverable=15;activeStrike(M_LP);tick();
    check(G.f[0].hp>90 && G.f[0].recoverable<15,"successful offense recovers grey health");
    setup();G.f[1].state=ST_LAUNCH;G.f[1].pos.y=0.5f;G.f[1].vy=-1;G.f[1].juggle=3;activeStrike(M_TORNADO);tick();
    check(G.f[1].tornadoUsed && G.f[1].vy>6,"first tornado extends an airborne combo");
    G.hitstop=0;G.f[1].pos.y=0.5f;G.f[1].vy=-1;activeStrike(M_TORNADO);tick();
    check(G.f[1].vy<6,"second tornado cannot grant another extension");
    setup();activeStrike(M_DLK);tick({},direction(3));check(G.f[0].state==ST_LAUNCH && G.f[1].hp==MAX_HP,"fresh down-forward low parry launches a low attacker");
    setup();G.f[0].state=ST_LAUNCH;G.f[0].pos.y=0.04f;G.f[0].vy=-5;tick(punch());
    check(G.f[0].state==ST_TECH && G.f[0].pos.y==0,"landing punch buffers a tech roll");
    frames(20);check(G.f[0].state==ST_IDLE,"tech roll returns to actionable neutral");
    setup(6);Input dashBack;dashBack.dashB=true;tick(dashBack);frames(3);tick(direction(1));
    check(G.f[0].state==ST_CROUCH,"backdash can be crouch-canceled for backdash sequences");
    setup(6);tick(jump);check(G.f[0].state==ST_PREJUMP && G.f[0].pos.y==0,"jump has grounded startup before takeoff");
    setup(6);G.projectiles.push_back({{-0.15f,PROJECTILE_HEIGHT,0},{9,0,0},0,0,100,false});G.projectiles.push_back({{0.15f,PROJECTILE_HEIGHT,0},{-9,0,0},1,0,100,false});tick();
    check(G.projectiles.empty() && G.f[0].hp==MAX_HP && G.f[1].hp==MAX_HP,"opposing projectiles clash before hitting fighters");
    setup(6);G.f[1].pos.z=2;G.projectiles.push_back({{0,PROJECTILE_HEIGHT,0},{9,0,0},0,0,100,false});frames(100);
    check(G.f[1].hp==MAX_HP,"a sidestepped projectile retains its original trajectory and misses");
    setup();G.f[1].hp=1;G.f[1].burnout=300;G.f[1].drive=0;
    G.projectiles.push_back({G.f[1].pos+Vector3{-0.15f,PROJECTILE_HEIGHT,0},{9,0,0},0,0,100,false});tick({},stableGuard);
    check(G.f[1].ko,"special chip can KO a burned-out defender");
    setup(6);G.paused=true;int heatClock=G.f[0].heatFrames=300;G.f[0].burnout=100;tick();
    check(G.f[0].heatFrames==heatClock && G.f[0].burnout==100,"pause freezes Heat and burnout clocks");
    setup();frames(4,{},heldParry);tick();activeStrike(M_THROW);tick();
    check(G.f[1].state==ST_THROWN,"a throw punishes parry release recovery");
    setup(6);G.f[0].serial=2;activeStrike(M_LP);
    G.projectiles.push_back({G.f[1].pos+Vector3{-0.15f,PROJECTILE_HEIGHT,0},{9,0,0},0,1,100,false});tick();
    check(!G.f[0].moveHit && G.f[0].contact==0,"an older projectile cannot confirm an unrelated normal");
    G.hitstop=0;tick(special);check(G.f[0].move==M_LP,"old projectile contact cannot unlock a whiff cancel");
    setup(6);G.f[1].hp=2;Input lowGuard;lowGuard.back=lowGuard.down=true;
    G.projectiles.push_back({G.f[1].pos+Vector3{-0.15f,PROJECTILE_HEIGHT,0},{9,0,0},0,0,100,false});tick({},lowGuard);
    check(G.f[1].hp==1 && !G.f[1].ko && G.f[1].state==ST_BLOCK,"crouch guard blocks specials and ordinary chip cannot KO");
    setup(6);tick({},jump);frames(13);
    check(G.f[1].pos.y>PROJECTILE_HEIGHT+PROJECTILE_HALF_HEIGHT,"jump arc clears the full projectile volume");
    G.projectiles.push_back({G.f[1].pos+Vector3{-0.50f,PROJECTILE_HEIGHT-G.f[1].pos.y,0},{9,0,0},0,0,100,false});frames(8);
    check(G.f[1].hp==MAX_HP && !G.projectiles.empty(),"well-timed jumping avoids a travelling wave");
    setup();G.f[0].pos={ARENA_R-1,0,0};G.f[1].pos={ARENA_R-0.1f,0,0};
    G.f[1].burnout=300;G.f[1].drive=0;activeStrike(M_IMPACT);tick({},stableGuard);
    check(G.f[1].state==ST_STAGGER && G.f[1].timer==75,"blocked corner Impact stuns a burned-out defender");
    setup();G.f[0].heatAvailable=false;G.f[0].heatFrames=400;activeStrike(M_FRP);tick({},stableGuard);G.hitstop=0;tick(rush);
    check(G.f[0].state==ST_RUSH && G.f[0].heatFrames==0 && G.f[0].drive==6,"Heat Dash consumes Heat without spending Drive");
    setup(6);G.f[0].ko=true;G.f[0].hp=0;G.f[0].recoverable=20;G.f[0].state=ST_KO;
    G.projectiles.push_back({G.f[1].pos+Vector3{-0.15f,PROJECTILE_HEIGHT,0},{9,0,0},0,0,100,false});tick();
    check(G.f[0].hp==0 && G.f[1].hp<MAX_HP,"a KO owner's projectile can connect without reviving its owner");
    G=Game();G.mode=2;StartMatch();
    for(int n=0;n<18000;++n) {
        Input a,b;
        if(G.phase==PH_FIGHT && !G.hitstop && !G.slowmo) {
            a=ThinkAI(G.ai[0],G.f[0],G.f[1],2);b=ThinkAI(G.ai[1],G.f[1],G.f[0],2);
        }
        tick(a,b);
        for(const auto &f:G.f) {
            if(!std::isfinite(f.pos.x)||!std::isfinite(f.pos.y)||!std::isfinite(f.pos.z)||f.pos.y<0||f.hp<0||f.hp>MAX_HP||f.drive<0||f.drive>6.001f||f.heatFrames<0||f.burnout<0||f.recoverable<0||G.projectiles.size()>2)
                check(false,"long CPU match preserves physics invariants");
        }
        if(G.phase==PH_MATCH_END) StartMatch();
    }
    check(true,"18000-frame CPU combat simulation remains finite and grounded");
    std::printf("PASS: %d combat checks\n",checks);
}

