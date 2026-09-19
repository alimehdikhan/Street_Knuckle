#include "font.h"

#include <cstring>
#include <cctype>
#include <cmath>
#include <algorithm>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Each glyph is 7 rows of 5 bits, most significant bit leftmost.
#define G(a, b, c, d, e, f, g) { 0b##a, 0b##b, 0b##c, 0b##d, 0b##e, 0b##f, 0b##g }

// Index order is the ASCII range 32..90 (space through 'Z'), which covers
// everything the HUD prints.
static const unsigned char GLYPHS[][7] = {
    G(00000, 00000, 00000, 00000, 00000, 00000, 00000),  // 32 space
    G(00100, 00100, 00100, 00100, 00100, 00000, 00100),  // 33 !
    G(01010, 01010, 00000, 00000, 00000, 00000, 00000),  // 34 "
    G(01010, 11111, 01010, 01010, 01010, 11111, 01010),  // 35 #
    G(00100, 01111, 10100, 01110, 00101, 11110, 00100),  // 36 $
    G(11001, 11010, 00010, 00100, 01000, 01011, 10011),  // 37 %
    G(01100, 10010, 10100, 01000, 10101, 10010, 01101),  // 38 &
    G(00100, 00100, 01000, 00000, 00000, 00000, 00000),  // 39 '
    G(00010, 00100, 01000, 01000, 01000, 00100, 00010),  // 40 (
    G(01000, 00100, 00010, 00010, 00010, 00100, 01000),  // 41 )
    G(00000, 10101, 01110, 11111, 01110, 10101, 00000),  // 42 *
    G(00000, 00100, 00100, 11111, 00100, 00100, 00000),  // 43 +
    G(00000, 00000, 00000, 00000, 01100, 01100, 01000),  // 44 ,
    G(00000, 00000, 00000, 11111, 00000, 00000, 00000),  // 45 -
    G(00000, 00000, 00000, 00000, 00000, 01100, 01100),  // 46 .
    G(00001, 00010, 00010, 00100, 01000, 01000, 10000),  // 47 /
    G(01110, 10001, 10011, 10101, 11001, 10001, 01110),  // 48 0
    G(00100, 01100, 00100, 00100, 00100, 00100, 01110),  // 49 1
    G(01110, 10001, 00001, 00010, 00100, 01000, 11111),  // 50 2
    G(11111, 00010, 00100, 00010, 00001, 10001, 01110),  // 51 3
    G(00010, 00110, 01010, 10010, 11111, 00010, 00010),  // 52 4
    G(11111, 10000, 11110, 00001, 00001, 10001, 01110),  // 53 5
    G(00110, 01000, 10000, 11110, 10001, 10001, 01110),  // 54 6
    G(11111, 00001, 00010, 00100, 01000, 01000, 01000),  // 55 7
    G(01110, 10001, 10001, 01110, 10001, 10001, 01110),  // 56 8
    G(01110, 10001, 10001, 01111, 00001, 00010, 01100),  // 57 9
    G(00000, 01100, 01100, 00000, 01100, 01100, 00000),  // 58 :
    G(00000, 01100, 01100, 00000, 01100, 01100, 01000),  // 59 ;
    G(00010, 00100, 01000, 10000, 01000, 00100, 00010),  // 60 <
    G(00000, 00000, 11111, 00000, 11111, 00000, 00000),  // 61 =
    G(01000, 00100, 00010, 00001, 00010, 00100, 01000),  // 62 >
    G(01110, 10001, 00001, 00010, 00100, 00000, 00100),  // 63 ?
    G(01110, 10001, 10111, 10101, 10111, 10000, 01110),  // 64 @
    G(01110, 10001, 10001, 11111, 10001, 10001, 10001),  // 65 A
    G(11110, 10001, 10001, 11110, 10001, 10001, 11110),  // 66 B
    G(01110, 10001, 10000, 10000, 10000, 10001, 01110),  // 67 C
    G(11110, 10001, 10001, 10001, 10001, 10001, 11110),  // 68 D
    G(11111, 10000, 10000, 11110, 10000, 10000, 11111),  // 69 E
    G(11111, 10000, 10000, 11110, 10000, 10000, 10000),  // 70 F
    G(01110, 10001, 10000, 10111, 10001, 10001, 01111),  // 71 G
    G(10001, 10001, 10001, 11111, 10001, 10001, 10001),  // 72 H
    G(11111, 00100, 00100, 00100, 00100, 00100, 11111),  // 73 I
    G(00111, 00010, 00010, 00010, 00010, 10010, 01100),  // 74 J
    G(10001, 10010, 10100, 11000, 10100, 10010, 10001),  // 75 K
    G(10000, 10000, 10000, 10000, 10000, 10000, 11111),  // 76 L
    G(10001, 11011, 10101, 10101, 10001, 10001, 10001),  // 77 M
    G(10001, 11001, 10101, 10011, 10001, 10001, 10001),  // 78 N
    G(01110, 10001, 10001, 10001, 10001, 10001, 01110),  // 79 O
    G(11110, 10001, 10001, 11110, 10000, 10000, 10000),  // 80 P
    G(01110, 10001, 10001, 10001, 10101, 10010, 01101),  // 81 Q
    G(11110, 10001, 10001, 11110, 10100, 10010, 10001),  // 82 R
    G(01111, 10000, 10000, 01110, 00001, 00001, 11110),  // 83 S
    G(11111, 00100, 00100, 00100, 00100, 00100, 00100),  // 84 T
    G(10001, 10001, 10001, 10001, 10001, 10001, 01110),  // 85 U
    G(10001, 10001, 10001, 10001, 10001, 01010, 00100),  // 86 V
    G(10001, 10001, 10001, 10101, 10101, 11011, 10001),  // 87 W
    G(10001, 10001, 01010, 00100, 01010, 10001, 10001),  // 88 X
    G(10001, 10001, 01010, 00100, 00100, 00100, 00100),  // 89 Y
    G(11111, 00001, 00010, 00100, 01000, 10000, 11111),  // 90 Z
};
#undef G

static const int FIRST_CHAR = 32;
static const int GLYPH_COUNT = (int)(sizeof(GLYPHS) / sizeof(GLYPHS[0]));
static const int CELL = 8;                 // padded cell, keeps bilinear taps clean
static const int COLS = 16;
static const int WHITE_INDEX = GLYPH_COUNT;      // one extra cell, fully opaque

static int g_atlasW = 0, g_atlasH = 0;
static unsigned char *g_atlas = nullptr;
static bool g_smooth = false;
static int g_widths[95]{};
static int g_cellHeight=96;

static bool BuildSmoothAtlas() {
    const int w=2048,h=768,cell=128;
    HDC dc=CreateCompatibleDC(nullptr);
    if(!dc)return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void *pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    HFONT font=CreateFontW(-96,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,ANSI_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Bahnschrift");
    if(!bitmap||!font) {
        if(bitmap)DeleteObject(bitmap);if(font)DeleteObject(font);DeleteDC(dc);return false;
    }
    HGDIOBJ oldBitmap=SelectObject(dc,bitmap),oldFont=SelectObject(dc,font);
    TEXTMETRICW metrics{}; GetTextMetricsW(dc,&metrics);
    g_cellHeight=std::min(124,int(metrics.tmHeight));
    memset(pixels,0,size_t(w)*h*4);
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
    for(int i=0;i<95;++i) {
        char c=char(i+32);SIZE size{};GetTextExtentPoint32A(dc,&c,1,&size);
        g_widths[i]=int(size.cx);
        TextOutA(dc,(i%16)*cell+2,(i/16)*cell+2,&c,1);
    }
    GdiFlush();
    g_atlasW=w;g_atlasH=h;g_atlas=new unsigned char[size_t(w)*h];
    auto *bgra=static_cast<unsigned char*>(pixels);
    for(int i=0;i<w*h;++i)g_atlas[i]=bgra[i*4];
    // Last atlas cell is reserved for solid UI geometry.
    for(int y=642;y<652;++y)for(int x=1922;x<1932;++x)g_atlas[y*w+x]=255;
    SelectObject(dc,oldFont);SelectObject(dc,oldBitmap);
    DeleteObject(font);DeleteObject(bitmap);DeleteDC(dc);
    g_smooth=true;return true;
}

const unsigned char *FontBuildAtlas(int &width, int &height) {
    if (g_atlas) {
        width = g_atlasW;
        height = g_atlasH;
        return g_atlas;
    }
    if(BuildSmoothAtlas()) {width=g_atlasW;height=g_atlasH;return g_atlas;}
    int cells = GLYPH_COUNT + 1;
    int rows = (cells + COLS - 1) / COLS;
    g_atlasW = COLS * CELL;
    g_atlasH = rows * CELL;
    g_atlas = new unsigned char[g_atlasW * g_atlasH];
    memset(g_atlas, 0, (size_t)g_atlasW * g_atlasH);

    for (int i = 0; i < GLYPH_COUNT; i++) {
        int cx = (i % COLS) * CELL, cy = (i / COLS) * CELL;
        for (int r = 0; r < 7; r++) {
            unsigned char bits = GLYPHS[i][r];
            for (int c = 0; c < 5; c++) {
                if (bits & (1 << (4 - c))) g_atlas[(cy + r) * g_atlasW + cx + c] = 255;
            }
        }
    }
    // solid cell for rectangles
    int wx = (WHITE_INDEX % COLS) * CELL, wy = (WHITE_INDEX / COLS) * CELL;
    for (int r = 0; r < CELL; r++)
        for (int c = 0; c < CELL; c++) g_atlas[(wy + r) * g_atlasW + wx + c] = 255;

    width = g_atlasW;
    height = g_atlasH;
    return g_atlas;
}

static GlyphUV CellUV(int index, int w, int h) {
    int cx = (index % COLS) * CELL, cy = (index / COLS) * CELL;
    return { (float)cx / w, (float)cy / h, (float)(cx + 5) / w, (float)(cy + 7) / h };
}

GlyphUV FontGlyph(char c) {
    int w, h;
    FontBuildAtlas(w, h);
    unsigned char u = (unsigned char)c;
    if(g_smooth) {
        int i=u>=32 && u<127 ? u-32 : 0;
        int x=(i%16)*128+2,y=(i/16)*128+2;
        return {float(x)/w,float(y)/h,float(x+g_widths[i])/w,float(y+g_cellHeight)/h};
    }
    if (u >= 'a' && u <= 'z') u = (unsigned char)(u - 'a' + 'A');
    int idx = (int)u - FIRST_CHAR;
    if (idx < 0 || idx >= GLYPH_COUNT) idx = 0;
    return CellUV(idx, w, h);
}

GlyphUV FontWhite() {
    int w, h;
    FontBuildAtlas(w, h);
    if(g_smooth)return {1926.0f/w,646.0f/h,1926.0f/w,646.0f/h};
    int cx = (WHITE_INDEX % COLS) * CELL, cy = (WHITE_INDEX / COLS) * CELL;
    // sample the middle of the solid cell, well away from its edges
    float u = (float)(cx + 3) / w, v = (float)(cy + 3) / h;
    return { u, v, u, v };
}

int FontMeasure(const char *s, int scale) {
    if (!s || !*s) return 0;
    int width=0;for(const char *p=s;*p;++p)width+=FontAdvance(*p,scale);
    return width;
}

int FontAdvance(char c,int scale) {
    if(!g_smooth)return FontAdvance(scale);
    unsigned char u=(unsigned char)c;int i=u>=32&&u<127?u-32:0;
    return (int)std::ceil(g_widths[i]*(7.0f*scale/96.0f));
}
int FontGlyphW(char c,int scale) {return g_smooth?FontAdvance(c,scale):5*scale;}

float FontWidthPx(char c, float height) {
    int w,h; FontBuildAtlas(w,h);
    unsigned char u=(unsigned char)c;
    return g_smooth ? g_widths[u>=32 && u<127 ? u-32 : 0]*height/96.0f : height*5.0f/7.0f;
}
float FontMeasurePx(const char *s, float height, float widthScale, float tracking) {
    float width=0;
    if (!s || !*s) return 0;
    for (const char *p=s;*p;++p) width += FontWidthPx(*p,height)*widthScale+tracking;
    return width-tracking;
}

float FontHeightPx(float height) {return g_smooth?height*g_cellHeight/96.0f:height;}
