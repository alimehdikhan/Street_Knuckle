// Steel Knuckle's screen-space presentation. Coordinates use a 1280x720 safe area.
#include "ui.h"
#include "game.h"
#include "render.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
// Moonlit steel, parchment lettering, brass, and a cold rival accent.
const Color Ink{9,14,20,255}, Panel{16,24,32,244}, White{241,239,229,255};
const Color Muted{168,184,195,255}, Gold{221,184,116,255}, Ice{131,201,218,255};
const Color Line{68,83,94,255}, Red{239,100,79,255};
float scale=1,ox=0,oy=0;
float X(float x){return ox+x*scale;} float Y(float y){return oy+y*scale;}
void Rect(float x,float y,float w,float h,Color c){R::UIRect(X(x),Y(y),w*scale,h*scale,c);}
void HGrad(float x,float y,float w,float h,Color a,Color b){R::UIRectGradientH(X(x),Y(y),w*scale,h*scale,a,b);}
void VGrad(float x,float y,float w,float h,Color a,Color b){R::UIRectGradientV(X(x),Y(y),w*scale,h*scale,a,b);}
void Slant(float x,float y,float w,float h,float skew,Color a,Color b){if(w>0)R::UISlant(X(x),Y(y),w*scale,h*scale,skew*scale,a,b);}
void Outline(float x,float y,float w,float h,Color c){R::UIRectLines(X(x),Y(y),w*scale,h*scale,scale,c);}
float Width(const char *s,float px,float squeeze=1,float tracking=0){return FontMeasurePx(s,px,squeeze,tracking);}
void Type(const char *s,float x,float y,float px,Color c,float squeeze=1,float tracking=0,float italic=0){
    R::UITextStyled(s,X(x+1),Y(y+1),px*scale,Fade(Ink,c.a/255.0f*0.7f),squeeze,tracking*scale,italic);
    R::UITextStyled(s,X(x),Y(y),px*scale,c,squeeze,tracking*scale,italic);
}
void Right(const char *s,float x,float y,float px,Color c){Type(s,x-Width(s,px),y,px,c);}
void Center(const char *s,float x,float y,float px,Color c,float squeeze=1,float tracking=0,float italic=0){Type(s,x-Width(s,px,squeeze,tracking)*0.5f,y,px,c,squeeze,tracking,italic);}
void Diamond(float x,float y,float r,Color c){R::UIDiamond(X(x),Y(y),r*scale,c);}
void Key(const char *key,float x,float y,float w=32,Color color=Muted){
    Rect(x,y,w,25,{23,33,43,242});Outline(x,y,w,25,{86,103,115,255});
    Center(key,x+w*0.5f,y+3,17,color);
}
void Hint(const char *key,const char *label,float x,float y,float w=32){Key(key,x,y,w);Type(label,x+w+10,y+3,17,Muted);}
void Bottom(){VGrad(0,626,1280,94,{9,14,20,0},{9,14,20,244});Rect(48,658,1184,1,{97,110,117,90});}
const char *ModeName(){static const char *names[]={"Arcade","Local versus","Exhibition","Training"};return names[G.mode];}
const char *Difficulty(){static const char *names[]={"Rookie","Challenger","Veteran"};return names[G.difficulty];}

void Title(){
    HGrad(0,0,860,720,{7,12,19,251},{7,12,19,0});
    VGrad(0,0,1280,130,{7,12,19,125},{7,12,19,0});
    Rect(54,48,28,3,Gold);Type("THE NOCTURNE COURT",96,39,18,Muted,1,2);
    Type("STEEL",49,100,105,White,0.88f,1,0.09f);
    Type("KNUCKLE",48,184,105,White,0.88f,1,0.09f);
    Type("Own the moment.",57,301,24,Gold);
    static const char *names[]={"ARCADE","LOCAL VERSUS","EXHIBITION","TRAINING"};
    static const char *desc[]={"Challenge the CPU in a best-of-three match.","Two players. One arena. Settle it here.","Watch two CPU fighters go head to head.","Master your timing with a configurable rival."};
    for(int i=0;i<4;++i){
        float y=360+i*49;bool selected=G.mode==i;
        if(selected){Slant(52,y,416,43,12,Gold,{170,137,80,255});Rect(52,y+5,3,33,White);}
        Type(names[i],74,y+5,29,selected?Ink:White,0.91f,0.4f,0.03f);
        if(selected)Right("ENTER",448,y+12,16,Ink);
    }
    Type(desc[G.mode],59,568,18,Muted);
    char info[80];std::snprintf(info,sizeof(info),"CPU difficulty: %s",Difficulty());
    Type(info,59,600,18,Gold);
    // Keep the scene visible on the right, with quiet venue information.
    Right("NOCTURNE",1224,545,32,White);
    Right("Moonlit court",1224,584,19,Muted);
    Right("First to two rounds  /  60 seconds",1224,611,16,Gold);
    Bottom();Hint("ENTER","Fight",56,677,58);Hint("UP/DN","Mode",225,677,58);
    Hint("L/R","Difficulty",412,677,45);Hint("P","Controls",636,677);
    Hint("F4","Graphics",840,677);Hint("ESC","Exit",1092,677,42);
}

void Health(const Fighter &f,bool left){
    float x=left?52:724,w=504;Color accent=left?Gold:Ice;
    // Anchored toward the timer so each bar drains toward its fighter's side.
    Slant(x-5,68,w+10,31,left?12:-12,{65,78,88,255},{25,35,44,255});
    Slant(x,72,w,23,left?9:-9,{25,34,42,255},{32,41,48,255});
    float hp=Clampf(float(f.hp)/MAX_HP,0,1),lag=Clampf(f.hpLag/MAX_HP,0,1);
    auto fill=[&](float amount,Color a,Color b){
        float width=(w-8)*amount;
        Slant(left?x+4:x+w-4-width,75,width,17,left?6:-6,a,b);
    };
    fill(lag,{160,72,53,255},{188,105,71,255});
    if(f.recoverable>0)fill(Clampf(float(f.hp+f.recoverable)/MAX_HP,0,1),{95,122,139,255},{166,185,192,255});
    fill(hp,f.hp<MAX_HP*0.25f?Red:accent,f.hp<MAX_HP*0.25f?Color{255,161,115,255}:White);
    // Segment marks are subtle; the remaining health is the strongest shape.
    for(int i=1;i<4;++i)Rect(x+i*w/4,75,1,17,{8,14,20,88});
    if(left){Type(f.name,x,30,31,White,0.88f,1);Type("P1",x,106,15,accent,1,1);}
    else {Right(f.name,x+w,30,31,White);Right(G.mode==1?"P2":"CPU",x+w,106,15,accent);}
    char hpText[32];std::snprintf(hpText,sizeof(hpText),"%d / %d",f.hp,MAX_HP);
    if(left)Right(hpText,x+w-70,44,15,Muted);else Type(hpText,x+70,44,15,Muted);
    for(int i=0;i<ROUNDS_TO_WIN;++i)Diamond(left?x+w-15-i*24:x+15+i*24,49,6,i<f.wins?accent:Line);
    if(f.msgT>0){float alpha=std::min(1.0f,f.msgT/12.0f);if(left)Type(f.msg,x,170,22,Fade(accent,alpha),0.94f,1);else Right(f.msg,x+w,170,22,Fade(accent,alpha));}
    if(f.showT>0 && f.showCount>=2){
        float alpha=std::min(1.0f,f.showT/18.0f);char count[16],damage[32];
        std::snprintf(count,sizeof(count),"%02d",f.showCount);std::snprintf(damage,sizeof(damage),"%d damage",f.showDmg);
        if(left){Type(count,x,192,66,Fade(White,alpha),0.84f,0,0.06f);Type("HIT COMBO",x+88,211,18,Fade(accent,alpha));Type(damage,x+88,237,17,Fade(Muted,alpha));}
        else {Right(count,x+w,192,66,Fade(White,alpha));Right("HIT COMBO",x+w-92,211,18,Fade(accent,alpha));Right(damage,x+w-92,237,17,Fade(Muted,alpha));}
    }
}
void Rage(const Fighter &f,bool left){
    float x=left?52:988;Color accent=f.raging?Gold:(left?Gold:Ice);
    float fill=240*float(f.meter)/METER_MAX;
    Rect(x,602,240,5,{59,72,83,230});
    HGrad(left?x:x+240-fill,602,fill,5,accent,f.raging?White:accent);
    for(int i=1;i<4;++i)Rect(x+i*60,602,2,5,Ink);
    const char *label=f.raging?"RAGE ART READY":"RAGE";
    if(left){Type(label,x,615,17,accent,1,1);if(f.raging)Type("L / U+I",x,639,15,White);}
    else {Right(label,x+240,615,17,accent);if(f.raging)Right(G.mode==1?"NUM 3 / 4+5":"FULL METER",x+240,639,15,White);}
}
void Resources(const Fighter &f,bool left){
    float x=left?102:854;Color drive=f.burnout?Red:Color{111,224,187,255};
    float amount=f.burnout?6.0f*(1-f.burnout/600.0f):f.drive;
    for(int i=0;i<6;++i){float bx=x+(left?i:5-i)*46;Rect(bx,112,42,6,{48,64,73,240});
        float fill=42*Clampf(amount-i,0,1);Rect(left?bx:bx+42-fill,112,fill,6,drive);}
    float hx=left?102:926;Rect(hx,136,204,4,{67,73,82,240});
    float heat=f.heatFrames>0?std::min(1.0f,float(f.heatFrames)/f.heatDuration):f.heatAvailable?1:0;
    Rect(left?hx:hx+204*(1-heat),136,204*heat,4,Gold);
    char label[64];
    if(f.burnout)std::snprintf(label,sizeof(label),"BURNOUT  %.1fs",f.burnout/60.0f);
    else std::snprintf(label,sizeof(label),"DRIVE  %.1f / 6",f.drive);
    char heatLabel[48];
    if(f.heatFrames>0)std::snprintf(heatLabel,sizeof(heatLabel),"HEAT  %.1fs",f.heatFrames/60.0f);
    else std::snprintf(heatLabel,sizeof(heatLabel),"%s",f.heatAvailable?"HEAT READY":"HEAT USED");
    if(left){Type(label,390,105,14,drive);Type(heatLabel,102,143,14,Gold);}
    else {Right(label,838,105,14,drive);Right(heatLabel,1130,143,14,Gold);}
}
void FightHUD(){
    VGrad(0,0,1280,180,{6,11,18,230},{6,11,18,0});
    Health(G.f[0],true);Health(G.f[1],false);
    Resources(G.f[0],true);Resources(G.f[1],false);
    Slant(581,28,118,77,0,{16,23,30,225},{16,23,30,225});
    Rect(607,26,66,2,Gold);
    int seconds=(G.timer+59)/60;char timer[12];std::snprintf(timer,sizeof(timer),G.mode==3?"--":"%02d",seconds);
    Center(timer,640,28,61,seconds<=10?Red:White,0.85f);
    char round[32];std::snprintf(round,sizeof(round),"ROUND %02d",G.round);
    Center(G.mode==3?"PRACTICE":round,640,98,15,Muted,1,1.2f);
    Center(ModeName(),640,126,15,Muted);
    Bottom();Rage(G.f[0],true);Rage(G.f[1],false);
    Hint("ESC","Pause",52,677,42);
    char graphics[64];std::snprintf(graphics,sizeof(graphics),"F4  %s",R::QualityName());Right(graphics,1228,681,16,Muted);
}
void Training(){
    const Fighter &f=G.f[0];const MoveDef &m=MOVES[f.move];
    const char *state=f.state==ST_ATTACK?(f.moveFrame<=m.startup?"Startup":f.moveFrame<=m.startup+m.active?"Active":"Recovery"):f.state==ST_LAND?"Landing":f.state==ST_BLOCK?"Guarding":f.state==ST_HIT?"Hit stun":f.state==ST_PARRY?"Parrying":f.state==ST_PARRY_END?"Parry recovery":f.state==ST_RUSH?"Rushing":f.state==ST_TECH?"Tech roll":f.state==ST_PREJUMP?"Jump startup":f.state==ST_LAUNCH?"Airborne":"Ready";
    Rect(52,453,347,125,Panel);Rect(52,453,3,125,Gold);
    Type(f.move?m.name:"Move analysis",68,464,24,White);
    Type(state,68,498,17,Gold);
    char line[100];std::snprintf(line,sizeof(line),"Start %df   Active %df   Recover %df",m.startup,m.active,m.recovery);
    Type(f.move?line:"Press an attack to inspect its timing.",68,525,16,Muted);
    const char *contact=f.contact==1?"Hit":f.contact==2?"Blocked":f.contact==3?"Parried":f.contact==4?"Armored":"No contact";
    if(f.contact==1 && f.frameAdvantage==999)std::snprintf(line,sizeof(line),"Hit   Launch / knockdown");
    else if(f.contact==1 || f.contact==2)std::snprintf(line,sizeof(line),"%s   %+df advantage",contact,f.frameAdvantage);
    else std::snprintf(line,sizeof(line),"%s",contact);
    Type(line,68,549,16,Ice);
    static const char *dummy[]={"Standing","Standing guard","Low guard","Sparring"};
    Rect(426,601,428,53,Panel);char label[64];std::snprintf(label,sizeof(label),"F5   Rival: %s",dummy[G.dummy]);
    Center(label,640,608,18,White);Center("R  Reset     F6  Hitboxes     Health auto-refills",640,634,15,Muted);
}
void Controls(){
    const char *action[]={"Move / guard","Jump / crouch","Sidestep","Left / right punch","Left / right kick","Throw / rage art"};
    const char *p1[]={"A / D","W / S","Q / E","U / I","J / K","O / L"};
    const char *p2[]={"Left / right","Up / down","Comma / period","Num 4 / 5","Num 1 / 2","Num 6 / 3"};
    const char *pad[]={"D-pad / left stick","D-pad up / down","LB / RB","X / Y","A / B","X+A / X+Y"};
    float cols[]={70,411,614,901};
    Type("ACTION",cols[0],172,15,Muted,1,1);Type("PLAYER 1",cols[1],172,15,Gold,1,1);
    Type("PLAYER 2",cols[2],172,15,Ice,1,1);Type("CONTROLLER",cols[3],172,15,Muted,1,1);
    for(int i=0;i<6;++i){float y=210+i*49;if(i%2==0)Rect(54,y-3,1172,46,{22,32,41,160});
        Type(action[i],cols[0],y+5,23,White);Type(p1[i],cols[1],y+5,23,Gold);Type(p2[i],cols[2],y+5,23,Ice);Type(pad[i],cols[3],y+7,20,White);}
    Rect(54,524,1172,1,Line);
    Type("DEFENSE",70,546,16,Gold,1,1);Type("Hold away to block. Down + away blocks lows.",70,574,19,White);
    Type("Hold V / LT to parry. Punch to break a throw.",70,604,18,Muted);
    Type("OFFENSE",704,546,16,Ice,1,1);Type("U > I > J > K chains a confirmed hit into a finisher.",704,574,18,White);
    Type("S + I launches. Back + J extends a juggle once.",704,604,18,Muted);
}
void Moves(){
    for(int col=0;col<2;++col){float x=62+col*606;
        Type("INPUT",x,172,15,Muted,1,1);Type("MOVE",x+112,172,15,Muted,1,1);Right("LEVEL    DMG   START",x+548,172,15,Muted);
        for(int row=0;row<8;++row){int id=G.movePage*16+col*8+row+1;if(id>=M_COUNT)break;const auto &m=MOVES[id];float y=211+row*43;
            if(row%2==0)Rect(x-10,y-3,564,41,{22,32,41,180});Type(m.input,x,y+5,18,Gold);Type(m.name,x+112,y+4,20,White);
            const char *level=m.level==LV_HIGH?"HIGH":m.level==LV_MID?"MID":m.level==LV_LOW?"LOW":m.level==LV_SPECIAL?"SP":"GRAB";
            Right(level,x+405,y+7,15,Ice);char n[12];std::snprintf(n,sizeof(n),"%d",m.damage);Right(n,x+478,y+5,18,White);
            std::snprintf(n,sizeof(n),"%df",m.startup);Right(n,x+548,y+5,18,White);
        }
    }
    Rect(54,574,1172,1,Line);
    char page[64];std::snprintf(page,sizeof(page),"<  PAGE %d / %d  >   Left / right to change",G.movePage+1,(M_COUNT-2)/16+1);
    Type(page,64,594,19,Gold);
    Type("F/B: toward/away. SP blocks high or low. See Systems for motion commands.",64,625,19,White);
}
void Systems(){
    const char *names[]={"Special move","Heat burst / smash","Drive Impact","Drive Parry","Drive Rush","Drive Reversal"};
    const char *p1[]={"F","G","H","Hold V","B / 6,6","H while blocking"};
    const char *p2[]={"Num 0","Num 7","Num 8","Hold Num 9","Num . / 6,6","Num 8 in guard"};
    const char *pad[]={"R3","L3","RT","Hold LT","LT + 6,6","RT in guard"};
    const char *rules[]={"Hold down: uppercut. Back: spin.","Once per round. Press again to finish.","1 Drive. Absorbs two strikes.","Blocks strikes. Vulnerable to throws.","1 Drive raw. 3 Drive to cancel.","2 Drive. Escape block pressure."};
    Type("ABILITY",70,172,15,Muted,1,1);Type("PLAYER 1",320,172,15,Gold);Type("PLAYER 2",500,172,15,Ice);Type("PAD",692,172,15,Muted);
    for(int i=0;i<6;++i){float y=210+i*48;if(i%2==0)Rect(54,y-3,1172,45,{22,32,41,160});
        Type(names[i],70,y+6,21,White);Type(p1[i],320,y+8,18,Gold);Type(p2[i],500,y+8,18,Ice);Type(pad[i],692,y+8,17,Muted);Type(rules[i],850,y+9,16,White);}
    Rect(54,519,1172,1,Line);
    Type("MOTION INPUTS",70,538,16,Gold,1,1);
    Type("236 + punch: wave    623 + punch: uppercut    214 + kick: spin",70,565,20,White);
    Type("2 down / 3 down-forward / 6 forward / 1 down-back / 4 back. Directions face your rival.",70,595,18,Muted);
    Type("Use both punches or kicks for Overdrive (2 Drive). Down + forward parries lows. Punch on landing to tech.",70,625,17,Ice);
}
void Overlay(){
    R::UIRect(0,0,float(R::Width()),float(R::Height()),{6,11,17,243});
    bool title=G.phase==PH_TITLE;
    Type(title?"FIGHT MANUAL":"MATCH PAUSED",54,44,46,White,0.88f,1,0.03f);
    Right("STEEL KNUCKLE",1224,60,20,Gold);
    const char *tabs[]={"Match","Controls","Move list","Systems"};
    for(int i=title?1:0;i<4;++i){float x=54+(title?i-1:i)*191;Type(tabs[i],x+12,119,22,G.pauseTab==i?White:Muted);if(G.pauseTab==i)Rect(x,152,166,3,Gold);}
    Rect(54,156,1172,1,Line);
    if(G.pauseTab==1)Controls();else if(G.pauseTab==2)Moves();else if(G.pauseTab==3)Systems();else {
        const char *menu[]={"Resume fight","Controls","Move list","Restart match","Return to title"};
        for(int i=0;i<5;++i){float y=205+i*61;bool selected=G.menuSelection==i;
            if(selected)Slant(54,y,430,51,10,Gold,{168,137,86,255});
            Type(menu[i],77,y+7,29,selected?Ink:White,0.95f);
        }
        Rect(602,216,1,334,Line);Type("Stay composed.",660,212,37,White);
        Type("Guard the high. Read the low.",660,271,24,Gold);
        Type("Confirm a hit before committing to a string.",660,318,21,Muted);
        Type("A missed heavy attack leaves room for a punish.",660,354,21,Muted);
        char line[100];std::snprintf(line,sizeof(line),"Graphics: %s  [F4]",R::QualityName());Type(line,660,435,21,White);
        std::snprintf(line,sizeof(line),"Impact motion: %s  [F7]",G.reducedMotion?"Reduced":"Full");Type(line,660,478,21,White);
        Type("Reduced motion removes camera shake and impact flashes.",660,516,17,Muted);
    }
    Bottom();Hint("ESC",title?"Back":"Resume",54,677,42);Hint("TAB","Switch page",262,677,44);
    Hint("ENTER",title?"Begin match":"Select",520,677,60);Hint("F7",G.reducedMotion?"Motion: reduced":"Motion: full",817,677,35);
}
void Result(){
    int winner=G.f[0].wins>=ROUNDS_TO_WIN?0:1;
    VGrad(0,0,1280,720,{7,12,18,80},{7,12,18,244});
    Type("STEEL KNUCKLE",54,42,21,Gold,1,2);
    Right(ModeName(),1224,42,20,Muted);
    HGrad(260,220,380,370,{7,12,18,0},{7,12,18,235});HGrad(640,220,380,370,{7,12,18,235},{7,12,18,0});
    Center("MATCH COMPLETE",640,231,19,Gold,1,3);
    Center("VICTORY",640,272,88,White,0.84f,3,0.07f);
    char line[96];std::snprintf(line,sizeof(line),"%s wins",G.f[winner].name);Center(line,640,369,30,Gold);
    std::snprintf(line,sizeof(line),"%d   -   %d",G.f[0].wins,G.f[1].wins);Center(line,640,422,50,White,0.9f);
    Slant(492,506,296,49,10,Gold,{169,137,81,255});Center("ENTER   Rematch",642,516,25,Ink);
    Center("BACKSPACE   Return to title",640,580,20,Muted);
}
}
void DrawGameUI(float time){
    scale=std::min(float(R::Width())/1280,float(R::Height())/720);ox=(R::Width()-1280*scale)*0.5f;oy=(R::Height()-720*scale)*0.5f;
    if(G.paused){Overlay();return;}
    if(G.phase==PH_TITLE){Title();return;}
    if(G.phase==PH_MATCH_END){Result();return;}
    FightHUD();
    if(G.mode==3 && G.phase==PH_FIGHT)Training();
    if(G.bannerT>0 && G.phase!=PH_MATCH_END){
        float alpha=std::min(1.0f,G.bannerT/12.0f);
        float size=strcmp(G.banner,"K.O.")==0?114:66;
        if(G.mode==3)size=42;
        HGrad(270,295,370,125,{7,12,18,0},Fade(Ink,alpha*0.8f));HGrad(640,295,370,125,Fade(Ink,alpha*0.8f),{7,12,18,0});
        Center(G.banner,640,308,size,Fade(White,alpha),0.84f,3,0.07f);
        Rect(570,419,140,2,Fade(Gold,alpha));
    }
}
