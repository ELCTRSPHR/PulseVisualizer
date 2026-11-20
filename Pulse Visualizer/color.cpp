// Color helpers (HSV, themes, album palette)
#include "common.h"
#include "color.h"

// ============================================================================
//  COLOR
// ============================================================================
float gBright = 1.f, gHueOff = 0.f;
float gAlbumCur[3][3] = { { 1.00f,0.17f,0.84f },{ 0.48f,0.36f,1.00f },{ 0.00f,0.94f,1.00f } };
float gAlbumTgt[3][3] = { { 1.00f,0.17f,0.84f },{ 0.48f,0.36f,1.00f },{ 0.00f,0.94f,1.00f } };
D2D1_COLOR_F hsv(float h, float s, float v, float a) {
    h -= floorf(h); h *= 6.f; int i = (int)h; float f = h - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s), r, g, b;
    switch (i) {
    case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break; case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q;
    }
    return D2D1::ColorF(r, g, b, a);
}
D2D1_COLOR_F palette(int theme, float t, float phase, float a) {
    t = clampv(t, 0.f, 1.f); phase += gHueOff;
    float r, g, b;
    if (theme == 0) { D2D1_COLOR_F c = hsv(t * 0.83f + phase, 0.95f, 1.f, a); r = c.r; g = c.g; b = c.b; }
    else {
        static const float G[6][3][3] = {
            { { 1.00f,0.17f,0.84f },{ 0.48f,0.36f,1.00f },{ 0.00f,0.94f,1.00f } },  // Neon
            { { 1.00f,0.10f,0.20f },{ 1.00f,0.55f,0.00f },{ 1.00f,0.92f,0.25f } },  // Inferno
            { { 0.00f,1.00f,0.64f },{ 0.00f,0.71f,1.00f },{ 0.69f,0.00f,1.00f } },  // Aurora
            { { 1.00f,0.32f,0.18f },{ 0.87f,0.14f,0.46f },{ 0.48f,0.18f,0.97f } },  // Sunset
            { { 1.00f,1.00f,1.00f },{ 0.50f,0.86f,1.00f },{ 0.18f,0.42f,1.00f } },  // Ice
            { { 1.00f,1.00f,1.00f },{ 0.78f,0.82f,0.95f },{ 1.00f,1.00f,1.00f } },  // Mono
        };
        float tt = fmodf(t + phase * 0.5f, 2.f); if (tt > 1.f) tt = 2.f - tt;
        float pos = tt * 2.f; int i = std::min((int)pos, 1); float f = pos - i;
        const float(*g3)[3] = (theme == 7) ? gAlbumCur : G[clampv(theme - 1, 0, 5)];
        r = g3[i][0] + (g3[i + 1][0] - g3[i][0]) * f; g = g3[i][1] + (g3[i + 1][1] - g3[i][1]) * f; b = g3[i][2] + (g3[i + 1][2] - g3[i][2]) * f;
    }
    return D2D1::ColorF(r * gBright, g * gBright, b * gBright, a);
}
D2D1_COLOR_F whiten(D2D1_COLOR_F c, float t) { return D2D1::ColorF(c.r + (1 - c.r) * t, c.g + (1 - c.g) * t, c.b + (1 - c.b) * t, c.a); }