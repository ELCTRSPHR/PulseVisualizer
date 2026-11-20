// GPU shader pipeline (HLSL on the shared D3D11 device): HDR composite target + cinematic post-processing
#pragma once
#include "common.h"

struct PostParams {
    float bloom = 1.f, bloomSize = 1.f;         // strength + spread
    float streak = 0.f, ca = 0.f, grain = 0.f;  // streaks, chromatic aberration (px at the screen edge), film grain
    float grade = 0.f, vignette = 0.f, time = 0.f;
    float accent[3] = { 1.f, 1.f, 1.f };        // theme color for grading and streak tint
};

struct SceneParams {                            // shader backgrounds
    int style = 0;                              // 1 Flow, 2 Nebula, 3 Aurora
    float strength = 0.5f, time = 0.f, bass = 0.f, mid = 0.f, treble = 0.f, pulse = 0.f, level = 0.f;
    float c0[3] = {}, c1[3] = {}, c2[3] = {};   // three theme colors
};

class Gpu {
public:
    bool init(ID3D11Device* dev);               // compiles the shaders; false = shader effects unavailable
    bool ok() const { return ready; }
    void resize(int w, int h, ID2D1DeviceContext* d2d, IDXGISwapChain1* swap);   // (re)creates the HDR target + bloom chain
    void release();                             // drop every size-dependent resource (call before ResizeBuffers)
    ID2D1Bitmap1* comp() const { return compBmp.Get(); }   // D2D draws the world layer into this FP16 target
    void post(const PostParams& p);             // comp -> back buffer: bloom, streaks, CA, tone map, grade, vignette, grain
    void background(const SceneParams& p);      // fills the HDR world layer with a music-reactive background (before D2D draws)
    ID2D1Bitmap1* sceneBmp() const { return sceneBitmap.Get(); }   // FP16 visual-pass target; bloom glows from this layer only
private:
    struct Target { ComPtr<ID3D11Texture2D> tex; ComPtr<ID3D11RenderTargetView> rtv; ComPtr<ID3D11ShaderResourceView> srv; int w = 0, h = 0; };
    static const int LEVELS = 6;
    bool ready = false;
    int W = 0, H = 0;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ic;
    ComPtr<ID3D11VertexShader> vsFull;
    ComPtr<ID3D11PixelShader> psPrefilter, psDown, psUp, psStreak, psFinal, psBg;
    ComPtr<ID3D11Buffer> cb, cbScene;
    Target scn;                                     // FP16 visual layer (bars / ring / particles), shared with D2D
    ComPtr<ID2D1Bitmap1> sceneBitmap;
    void begin();                                   // common pipeline state for full-screen passes
    void sceneCB(const SceneParams& p, int w, int h);
    ComPtr<ID3D11SamplerState> samp;
    ComPtr<ID3D11BlendState> blendAdd;
    ComPtr<ID3D11RasterizerState> rsNoCull;
    Target hdr, bloom[LEVELS], streak[2];           // hdr: full-size FP16 world layer (D2D draws into it via compBmp)
    ComPtr<ID2D1Bitmap1> compBmp;
    ComPtr<ID3D11RenderTargetView> backRtv;
    bool makeTarget(Target& t, int w, int h);
    void pass(ID3D11PixelShader* ps, Target* dst, ID3D11RenderTargetView* rtv, int w, int h,
              ID3D11ShaderResourceView* s0, ID3D11ShaderResourceView* s1, ID3D11ShaderResourceView* s2,
              const void* cbData, bool additive, float blendK);
};
