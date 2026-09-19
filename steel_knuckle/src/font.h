// =============================================================================
//  font.h - antialiased system font atlas, with a built-in bitmap fallback.
//
//  raylib supplied the text rendering for free; here the HUD needs its own. The
//  face is uppercase-only with digits and the punctuation the HUD actually
//  uses, which suits the arcade look.
// =============================================================================
#pragma once

struct GlyphUV {
    float u0, v0, u1, v1;
};

// Builds the atlas. Returns the pixel buffer (one byte of coverage per texel),
// which stays valid for the life of the process.
const unsigned char *FontBuildAtlas(int &width, int &height);

// Glyph cell for a character. Unknown characters fall back to a blank cell.
GlyphUV FontGlyph(char c);

// A texel that is fully opaque, so solid rectangles can share the text pipeline.
GlyphUV FontWhite();

// Advance and line metrics, in pixels, for a given integer scale.
inline int FontAdvance(int scale) { return 6 * scale; }
int FontAdvance(char c, int scale);
int FontGlyphW(char c, int scale);
inline int FontLineHeight(int scale) { return 9 * scale; }
inline int FontGlyphW(int scale) { return 5 * scale; }
inline int FontGlyphH(int scale) { return 7 * scale; }

// Width of a whole string, in pixels.
int FontMeasure(const char *s, int scale);

float FontWidthPx(char c, float height);
float FontMeasurePx(const char *s, float height, float widthScale=1.0f, float tracking=0.0f);

float FontHeightPx(float height);
