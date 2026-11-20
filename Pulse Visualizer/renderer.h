// Direct2D renderer: setup, frame composition, primitives
#pragma once
#include "common.h"
#include "settings.h"
#include "spotify.h"
#include "gpu.h"

// ============================================================================
//  RENDERER
// ============================================================================
struct Particle { float x, y, vx, vy, life, maxLife, size, t; };
class Renderer {
public:
    int W = 1, H = 1;
    float ui = 1.f;
    bool init(HWND h);
    void resize(int w, int h);
    void render(float dt);
    bool shadersOk() const { return gpu.ok() && gpu.comp(); }
private:
    Gpu gpu;                                         // HLSL post-processing on the same D3D11 device
    ComPtr<ID3D11Device> d3d;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<ID2D1Device> d2dDev;
    ComPtr<ID2D1DeviceContext> ctx;
    ID2D1RenderTarget* rt = nullptr;
    ComPtr<ID2D1Bitmap1> back, scene, art;
    ComPtr<ID2D1BitmapBrush> artBrush, artBrushP;
    ComPtr<ID2D1Bitmap1> artPrev;                    // previous cover, kept while the new one fades in
    int artPrevW = 1, artPrevH = 1;
    float artFade = 1.f, artDrift = 0.f;
    ComPtr<ID2D1Effect> blurS, blurL, matS, matL, spR, spG, spB, artBlur, artMat, artBlurP, artMatP;
    ComPtr<ID2D1Effect> lyBlurS, lyBlurL, lyMatS, lyMatL;                  // lyric glow: tight + wide blur of the sung line
    ComPtr<ID2D1DeviceContext> rec;                                         // side context that records the sung line for the glow
    ComPtr<ID2D1SolidColorBrush> br;
    ComPtr<ID2D1StrokeStyle> roundStroke;
    ComPtr<IDWriteFactory> dw;
    ComPtr<IDWriteTextFormat> fL, fR, fTitle, fSmall, fMed;
    ComPtr<IDWriteTextFormat> fLy, fSp1, fSp2, fBtn, fIntro1, fIntro2;
    std::vector<Particle> parts;
    float partAcc = 0, baseY = 0, maxHt = 0, artR = 0;
    float sc = 1, phase = 0, rotAngle = 0, cycleT = 0, histAcc = 0;
    int theme = 0, histHead = 0, artW = 1, artH = 1;
    bool byLevel = false;
    unsigned artGen = 0;
    std::string lastTrackId;
    struct LyItem { ComPtr<IDWriteTextLayout> lay; int j = 0; float yc = 0, w = 0, h = 0, left = 0, s = 1.f, alpha = 1.f, w0 = 0.f; };
    LyricsData lyr;
    unsigned lyrGen = 0;
    double lyrAt = -100;
    LyItem lyItems[10];
    int lyN = 0;
    float lyMaxW = 800.f, lyAnchor = 0.f, lyMsgA = 0.f, lyX0 = 0.f, radShift = 0.f, sideT = 0.f, artCx = 0.f;
    bool lySide = false;
    int lyFontSel = -1;
    std::vector<LyricLine> lyD;
    float lyPos = -1.f, lyVel = 0.f, lyBarH = 0.f;
    bool lyInit = true;
    float lyAnchorS = -1.f;
    float visGain = 1.f, mixW[3] = { -1.f, 0.f, 0.f };                              // visual cross-fade weights: 0 Bars, 1 Radial, 2 NCS
    bool bgShader = false;                                                          // a shader background is under this frame
    void fillSceneColors(SceneParams& p);                                           // three theme colors for the scene shaders
    float hitK = 1.f, hitPx = 0.f, hitPy = 0.f;
    std::wstring lyMsg;
    float hist[HIST_N][MAXB];
    float uiDt = 0.016f, panelT = 0.f, tabT = 0.f, selY = -1.f, tabPX = -1.f, tabPW = 0.f;   // settings panel animation
    int lastTab = -1;
    float hovAmt[16] = {};
    float npC = 0.f, npBox[4] = { 0.f, 0.f, 0.f, 0.f };                                       // now-playing card: compact amount + last screen box
    void createTargets();
    void makeFonts(HWND h);
    void uploadArt(const ArtData& ad);
    void setc(D2D1_COLOR_F c, float bright = 1.f, float alpha = 1.f);
    void fillRR(float x0, float y0, float x1, float y1, float r);
    ComPtr<ID2D1PathGeometry> path(const std::vector<D2D1_POINT_2F>& p, bool fill, float closeY);
    void fillPoly(const std::vector<D2D1_POINT_2F>& p);
    D2D1_COLOR_F colorFor(int i, int B, float v);
    void drawBars(float punch);
    void drawRadial(float punch);
    void drawNCS(float punch);                                          // homage to the NoCopyrightSounds circle visualizer
    void updateParticles(float dt);
    void drawParticles();
    void drawCenterArt();
    void drawArtBackdrop(float dt);
    bool hasFont(const wchar_t* name);
    void makeLyricFont();
    ComPtr<ID2D1LinearGradientBrush> gradBrush(D2D1_COLOR_F a, D2D1_COLOR_F b, float x1, float y1, float x2, float y2, float alpha);
    static float interp(const float* a, int n, float x);
    void buildLyricRows();
    void prepareLyrics(float dt, double now);
    void drawLyrics();
    void drawLogo(float cx, float cy, float R, float t, float a);
    void drawSplashText(float t, float fade);
    void text(const std::wstring& s, float x, float y, float w, float h, IDWriteTextFormat* f, D2D1_COLOR_F c);
    void addHit(int id, float x0, float y0, float x1, float y1);
    void fieldInset(float x, float y, float w, float h, float r, D2D1_COLOR_F ac, float focus);
    static std::wstring valStr(const Param& p);
    void drawUI(double now);
    void drawPanel(D2D1_COLOR_F accent);
    void drawPane(float x, float y, float w, float h, float r, D2D1_COLOR_F ac, float weight = 1.f);   // plain see-through panel; weight scales "Panel opacity"
    bool hov(float x0, float y0, float x1, float y1) const;
    bool hovS(float x0, float y0, float x1, float y1) const;          // hover test in the current (possibly scaled) card space
    float hoverAnim(int key, bool on);                                  // smoothed 0..1 hover amount per control
    void arrowTri(float cx, float cy, float s, int dir, D2D1_COLOR_F c);
    void button(const std::wstring& label, float x, float y, float w, float h, int id, D2D1_COLOR_F ac, bool primary, bool enabled, float alpha);
    void drawPresetsTab(float x, float y, float w, D2D1_COLOR_F ac, float alpha);
    void iconPlay(float cx, float cy, float s);
    void iconPause(float cx, float cy, float s);
    void iconNext(float cx, float cy, float s);
    void iconPrev(float cx, float cy, float s);
    void pill(const std::wstring& label, float x, float y, float w, float h, bool active, int id, D2D1_COLOR_F ac);
    void drawNowPlaying(double now, D2D1_COLOR_F ac);
    void drawIntro(double now, D2D1_COLOR_F ac);
    bool introActive() const;                                           // would the song intro be showing for the current track?                       // big title / artist reveal when a song starts
    float qT = 0.f;                                                     // queue peek: open amount
    void drawSetup(double now, D2D1_COLOR_F ac);
};

extern Renderer gR;