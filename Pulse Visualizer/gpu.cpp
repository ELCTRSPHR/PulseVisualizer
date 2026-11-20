// GPU shader pipeline: HDR composite target, bloom chain, streaks, final cinematic grade, shader backgrounds
#include "common.h"
#include <d3dcompiler.h>
#include "gpu.h"

#pragma comment(lib, "d3dcompiler.lib")

// ---- HLSL (compiled at startup with d3dcompiler_47, which ships with Windows 10/11) ----
static const char* kPostHLSL = R"HLSL(
cbuffer Params : register(b0) {
    float2 texel; float2 res;           // source texel size, destination size in pixels
    float time, threshold, knee, intensity;
    float streak, grain, grade, vignette;
    float caPx, stride, scatter, thr2;
    float4 accent;
    float4 tint;
};
Texture2D t0 : register(t0);
Texture2D t1 : register(t1);
Texture2D t2 : register(t2);
SamplerState sl : register(s0);
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

V VSFull(uint id : SV_VertexID) {      // one triangle that covers the screen
    V o; float2 p = float2((id << 1) & 2, id & 2);
    o.uv = p; o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1); return o;
}
float maxc(float3 c) { return max(c.r, max(c.g, c.b)); }
float hash12(float2 p) { float3 q = frac(float3(p.xyx) * 0.1031); q += dot(q, q.yzx + 33.33); return frac((q.x + q.y) * q.z); }

// bright-pass with a soft knee, 4-tap box down to half size
float4 PSPrefilter(V i) : SV_Target {
    float2 d = texel;
    float3 c = t0.Sample(sl, i.uv + d * float2(-1, -1)).rgb + t0.Sample(sl, i.uv + d * float2(1, -1)).rgb
             + t0.Sample(sl, i.uv + d * float2(-1, 1)).rgb + t0.Sample(sl, i.uv + d * float2(1, 1)).rgb;
    c = min(c * 0.25, 6.0);                                   // tame single hot pixels
    float br = maxc(c);
    float rq = clamp(br - threshold + knee, 0, 2 * knee); rq = rq * rq / (4 * knee + 1e-4);
    float w = max(rq, br - threshold) / max(br, 1e-4);
    return float4(c * w, 1);
}
// dual-filter (Kawase) downsample
float4 PSDown(V i) : SV_Target {
    float2 d = texel;
    float3 s = t0.Sample(sl, i.uv).rgb * 4;
    s += t0.Sample(sl, i.uv - d).rgb + t0.Sample(sl, i.uv + d).rgb;
    s += t0.Sample(sl, i.uv + float2(d.x, -d.y)).rgb + t0.Sample(sl, i.uv - float2(d.x, -d.y)).rgb;
    return float4(s * 0.125, 1);
}
// dual-filter upsample (added on top of the next larger level with a blend factor)
float4 PSUp(V i) : SV_Target {
    float2 d = texel * 0.5 * stride;
    float3 s = t0.Sample(sl, i.uv + float2(-2 * d.x, 0)).rgb + t0.Sample(sl, i.uv + float2(2 * d.x, 0)).rgb
             + t0.Sample(sl, i.uv + float2(0, -2 * d.y)).rgb + t0.Sample(sl, i.uv + float2(0, 2 * d.y)).rgb;
    s += 2 * (t0.Sample(sl, i.uv + float2(-d.x, d.y)).rgb + t0.Sample(sl, i.uv + float2(d.x, d.y)).rgb
            + t0.Sample(sl, i.uv + float2(d.x, -d.y)).rgb + t0.Sample(sl, i.uv + float2(-d.x, -d.y)).rgb);
    return float4(s / 12, 1);
}
// horizontal anamorphic streak: 8 taps, spacing grows each pass
float4 PSStreak(V i) : SV_Target {
    float3 s = 0; float wsum = 0;
    [unroll] for (int k = 0; k < 8; k++) {
        float o = (k - 3.5) * stride, w = exp(-abs(k - 3.5) * 0.45);
        float3 c = t0.Sample(sl, i.uv + float2(o * texel.x, 0)).rgb;
        c = max(c - thr2, 0);                                  // only the brightest parts streak
        s += c * w; wsum += w;
    }
    return float4(s / wsum, 1);
}
// per-channel soft shoulder: linear up to 0.82, then rolls smoothly into 1.0, so hot cores still burn
// white like the classic look, just without the hard clipped edge
float3 tonemap(float3 c) {
    const float s = 0.82;
    float3 k = s + (1 - s) * (1 - exp(-(c - s) / (1 - s)));
    return c < s ? c : k;
}
float4 PSFinal(V i) : SV_Target {
    float2 uv = i.uv, dir = uv - 0.5;
    float2 off = dir * 2 * caPx * texel;                       // radial chromatic aberration (caPx at the screen edge)
    float3 c;
    c.r = t0.Sample(sl, uv - off).r;
    c.g = t0.Sample(sl, uv).g;
    c.b = t0.Sample(sl, uv + off).b;
    c += t1.Sample(sl, uv).rgb * intensity;                    // bloom
    c += t2.Sample(sl, uv).rgb * streak * tint.rgb;            // streaks
    c = tonemap(max(c, 0));
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(l.xxx, c, 1 + 0.15 * grade);                      // a touch more colour
    float3 sc = c * c * (3 - 2 * c);
    c = lerp(c, sc, 0.22 * grade);                             // gentle S-curve contrast
    c += accent.rgb * (0.025 * grade) * (1 - l) * (1 - l) * saturate(l * 8);   // shadows lean toward the theme colour (pure black stays black)
    float r = length(dir * res) / (0.5 * length(res));
    c *= 1 - vignette * 0.95 * saturate((r - 0.55) / 0.45);
    float n = hash12(uv * res + frac(time * 7.13) * float2(137.1, 71.7)) - 0.5;
    float mid = saturate(1 - abs(l - 0.45) * 1.7);
    c += n * grain * 0.055 * (0.3 + 0.7 * mid);                // film grain, strongest in the mid-tones
    c += (hash12(uv * res + 17.0) - 0.5) / 255.0;              // dither: no banding in dark gradients
    return float4(saturate(c), 1);
}
)HLSL";

// ---- scene shaders: music-reactive backgrounds ----
static const char* kSceneHLSL = R"HLSL(
cbuffer Scene : register(b0) {
    float2 res; float time; float strength;
    float bass, mid, treble, pulse;
    float level, style, pad0, pad1;
    float4 c0, c1, c2;
};
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

float hash21(float2 p) { p = frac(p * float2(123.34, 456.21)); p += dot(p, p + 45.32); return frac(p.x * p.y); }
float vnoise(float2 p) {
    float2 i = floor(p), f = frac(p), u = f * f * (3 - 2 * f);
    return lerp(lerp(hash21(i), hash21(i + float2(1, 0)), u.x), lerp(hash21(i + float2(0, 1)), hash21(i + 1), u.x), u.y);
}
float fbm(float2 p) {
    float s = 0, a = 0.5;
    [unroll] for (int k = 0; k < 5; k++) { s += a * vnoise(p); p = mul(float2x2(1.6, 1.2, -1.2, 1.6), p); a *= 0.5; }
    return s;
}
float3 pal3(float t) {                         // three theme colours, looping smoothly
    t = frac(t) * 3;
    return t < 1 ? lerp(c0.rgb, c1.rgb, smoothstep(0, 1, t)) : (t < 2 ? lerp(c1.rgb, c2.rgb, smoothstep(1, 2, t)) : lerp(c2.rgb, c0.rgb, smoothstep(2, 3, t)));
}

// ---- backgrounds: dark, slow and soft so the visual stays the star ----
float3 bgFlow(float2 uv) {
    float2 p = (uv - 0.5) * float2(res.x / res.y, 1) * 2.2;
    float t = time * 0.05;
    float2 q = float2(fbm(p + t), fbm(p + float2(5.2, 1.3) - t));
    float2 r = float2(fbm(p + 3 * q + float2(1.7, 9.2) + t * 1.5), fbm(p + 3 * q + float2(8.3, 2.8)));
    float f = fbm(p + (2.5 + 1.5 * bass) * r);
    float3 col = pal3(f * 1.2 + length(q) * 0.5 + time * 0.01);
    col *= smoothstep(0.2, 0.95, f) * (0.5 + 0.5 * length(r));
    return col * (0.22 + 0.16 * bass + 0.12 * pulse);
}
float3 bgNebula(float2 uv) {
    float2 p = (uv - 0.5) * float2(res.x / res.y, 1) * 2.0;
    float t = time * 0.03;
    float n = fbm(p * 1.3 + float2(t, -t * 0.6)), n2 = fbm(p * 2.7 - float2(t * 0.7, t));
    float cloud = pow(saturate(n * n2 * 2.4), 1.7);
    float3 col = lerp(c0.rgb, c2.rgb, saturate(n2 * 1.4)) * cloud + c1.rgb * pow(saturate((n - 0.45) * 2), 3) * 0.5;
    col *= 0.30 + 0.18 * bass + 0.12 * pulse;
    float2 cell = floor(uv * res / 22), fc = frac(uv * res / 22) - 0.5;     // sparse twinkling stars
    float h = hash21(cell), star = step(0.93, h);
    float2 sp = float2(hash21(cell + 7.1), hash21(cell + 3.3)) - 0.5;
    float d = length((fc - sp * 0.7) * 22);
    float tw = 0.55 + 0.45 * sin(time * (1.5 + 3 * h) + h * 40);
    col += star * saturate(1.3 - d) * tw * (0.25 + 0.5 * treble) * lerp(float3(1, 1, 1), c1.rgb, 0.4) * 0.6;
    return col;
}
float3 bgAurora(float2 uv) {
    float t = time * 0.06;
    float3 col = c0.rgb * 0.035 * (1 - uv.y);
    [unroll] for (int k = 0; k < 3; k++) {
        float yb = 0.32 + 0.14 * k + 0.16 * (fbm(float2(uv.x * 1.6 + t * (1 + k * 0.3), k * 3.1 + t * 0.5)) - 0.5) * (1 + mid);
        float d = uv.y - yb;
        float curtain = exp(-max(d, 0) * 40) * smoothstep(-0.30, 0, d);           // bright lower edge, rays fading upward
        float rays = 0.35 + 0.65 * pow(vnoise(float2(uv.x * 70 + t * 4 + k * 13, k * 7.0 + t)), 2);
        col += pal3(k * 0.33 + uv.x * 0.25 + t * 0.3) * curtain * rays * (0.30 - 0.06 * k);
    }
    return col * (0.75 + 0.45 * mid + 0.25 * pulse);
}
float4 PSBackground(V i) : SV_Target {
    float3 c = style < 1.5 ? bgFlow(i.uv) : (style < 2.5 ? bgNebula(i.uv) : bgAurora(i.uv));
    return float4(c * strength, 1);
}
)HLSL";

struct SceneCBData {
    float res[2], time, strength;
    float bass, mid, treble, pulse;
    float level, style, pad0, pad1;
    float c0[4], c1[4], c2[4];
};
static_assert(sizeof(SceneCBData) % 16 == 0, "constant buffer must be 16-byte aligned");

struct CBData {
    float texel[2], res[2];
    float time, threshold, knee, intensity;
    float streak, grain, grade, vignette;
    float caPx, stride, scatter, thr2;
    float accent[4];
    float tint[4];
};
static_assert(sizeof(CBData) % 16 == 0, "constant buffer must be 16-byte aligned");

static ComPtr<ID3DBlob> compileHlsl(const char* src, const char* entry, const char* target) {
    ComPtr<ID3DBlob> code, msg;
    HRESULT hr = D3DCompile(src, strlen(src), "pulse_post", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &msg);
    if (FAILED(hr)) {                                          // shows in a debugger's output window; the app falls back to plain Direct2D
        OutputDebugStringA((std::string("Pulse shader ") + entry + ": " + (msg ? (const char*)msg->GetBufferPointer() : "compile failed") + "\n").c_str());
        return nullptr;
    }
    return code;
}

bool Gpu::init(ID3D11Device* d) {
    ready = false; dev = d; dev->GetImmediateContext(&ic);
    ComPtr<ID3DBlob> b = compileHlsl(kPostHLSL, "VSFull", "vs_4_0");
    if (!b || FAILED(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsFull))) return false;
    struct { const char* name; ComPtr<ID3D11PixelShader>* ps; } list[] = {
        { "PSPrefilter", &psPrefilter }, { "PSDown", &psDown }, { "PSUp", &psUp }, { "PSStreak", &psStreak }, { "PSFinal", &psFinal } };
    for (auto& e : list) {
        b = compileHlsl(kPostHLSL, e.name, "ps_4_0");
        if (!b || FAILED(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, e.ps->GetAddressOf()))) return false;
    }
    struct { const char* name; ComPtr<ID3D11PixelShader>* ps; } list2[] = { { "PSBackground", &psBg } };
    for (auto& e : list2) {
        b = compileHlsl(kSceneHLSL, e.name, "ps_4_0");
        if (!b || FAILED(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, e.ps->GetAddressOf()))) return false;
    }
    D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = sizeof(CBData); bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(dev->CreateBuffer(&bd, nullptr, &cb))) return false;
    bd.ByteWidth = sizeof(SceneCBData);
    if (FAILED(dev->CreateBuffer(&bd, nullptr, &cbScene))) return false;
    D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(dev->CreateSamplerState(&sd, &samp))) return false;
    D3D11_BLEND_DESC bl = {}; auto& rtb = bl.RenderTarget[0];
    rtb.BlendEnable = TRUE; rtb.SrcBlend = D3D11_BLEND_BLEND_FACTOR; rtb.DestBlend = D3D11_BLEND_ONE; rtb.BlendOp = D3D11_BLEND_OP_ADD;
    rtb.SrcBlendAlpha = D3D11_BLEND_ZERO; rtb.DestBlendAlpha = D3D11_BLEND_ONE; rtb.BlendOpAlpha = D3D11_BLEND_OP_ADD; rtb.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(dev->CreateBlendState(&bl, &blendAdd))) return false;
    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    if (FAILED(dev->CreateRasterizerState(&rd, &rsNoCull))) return false;
    ready = true;
    return true;
}

bool Gpu::makeTarget(Target& t, int w, int h) {
    t = Target(); t.w = std::max(1, w); t.h = std::max(1, h);
    D3D11_TEXTURE2D_DESC td = {}; td.Width = t.w; td.Height = t.h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &t.tex))) return false;
    if (FAILED(dev->CreateRenderTargetView(t.tex.Get(), nullptr, &t.rtv))) return false;
    if (FAILED(dev->CreateShaderResourceView(t.tex.Get(), nullptr, &t.srv))) return false;
    const float z[4] = { 0, 0, 0, 1 }; ic->ClearRenderTargetView(t.rtv.Get(), z);
    return true;
}

void Gpu::release() {
    if (ic) { ID3D11ShaderResourceView* nul[3] = {}; ic->PSSetShaderResources(0, 3, nul); ic->OMSetRenderTargets(0, nullptr, nullptr); ic->Flush(); }
    compBmp.Reset(); backRtv.Reset(); hdr = Target(); sceneBitmap.Reset(); scn = Target();
    for (auto& t : bloom) t = Target();
    for (auto& t : streak) t = Target();
}

void Gpu::resize(int w, int h, ID2D1DeviceContext* d2d, IDXGISwapChain1* swap) {
    release();
    if (!ready) return;
    W = std::max(1, w); H = std::max(1, h);
    bool good = makeTarget(hdr, W, H) && makeTarget(scn, W, H);
    for (int i = 0; i < LEVELS && good; i++) good = makeTarget(bloom[i], W >> (i + 1), H >> (i + 1));
    for (int i = 0; i < 2 && good; i++) good = makeTarget(streak[i], bloom[1].w, bloom[1].h);
    ComPtr<ID3D11Texture2D> bb;
    if (good) good = SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&bb))) && SUCCEEDED(dev->CreateRenderTargetView(bb.Get(), nullptr, &backRtv));
    auto wrap = [&](Target& t, ComPtr<ID2D1Bitmap1>& out) {   // let Direct2D draw into a shader texture
        ComPtr<IDXGISurface> surf; t.tex.As(&surf);
        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_R16G16B16A16_FLOAT, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
        return surf && SUCCEEDED(d2d->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &out));
    };
    if (good) good = wrap(hdr, compBmp) && wrap(scn, sceneBitmap);
    if (!good) { release(); OutputDebugStringA("Pulse: could not create shader render targets\n"); }
}

void Gpu::pass(ID3D11PixelShader* ps, Target* dst, ID3D11RenderTargetView* rtv, int w, int h,
               ID3D11ShaderResourceView* s0, ID3D11ShaderResourceView* s1, ID3D11ShaderResourceView* s2,
               const void* cbData, bool additive, float blendK) {
    ID3D11ShaderResourceView* nul[3] = {};
    ic->PSSetShaderResources(0, 3, nul);                       // never read and write the same texture
    ID3D11RenderTargetView* r = dst ? dst->rtv.Get() : rtv;
    ic->OMSetRenderTargets(1, &r, nullptr);
    D3D11_VIEWPORT vp = { 0.f, 0.f, (float)w, (float)h, 0.f, 1.f }; ic->RSSetViewports(1, &vp);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ic->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, cbData, sizeof(CBData)); ic->Unmap(cb.Get(), 0); }
    const float bf[4] = { blendK, blendK, blendK, blendK };
    ic->OMSetBlendState(additive ? blendAdd.Get() : nullptr, bf, 0xFFFFFFFF);
    ID3D11ShaderResourceView* srv[3] = { s0, s1, s2 };
    ic->PSSetShaderResources(0, 3, srv);
    ic->PSSetShader(ps, nullptr, 0);
    ic->Draw(3, 0);
}

void Gpu::begin() {
    ic->IASetInputLayout(nullptr);
    ic->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ic->VSSetShader(vsFull.Get(), nullptr, 0);
    ic->PSSetSamplers(0, 1, samp.GetAddressOf());
    ic->RSSetState(rsNoCull.Get());
    ic->OMSetDepthStencilState(nullptr, 0);
}

void Gpu::sceneCB(const SceneParams& p, int w, int h) {
    SceneCBData c = {};
    c.res[0] = (float)w; c.res[1] = (float)h; c.time = p.time; c.strength = p.strength;
    c.bass = p.bass; c.mid = p.mid; c.treble = p.treble; c.pulse = p.pulse;
    c.level = p.level; c.style = (float)p.style;
    for (int k = 0; k < 3; k++) { c.c0[k] = p.c0[k]; c.c1[k] = p.c1[k]; c.c2[k] = p.c2[k]; }
    c.c0[3] = c.c1[3] = c.c2[3] = 1.f;
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ic->Map(cbScene.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, &c, sizeof(c)); ic->Unmap(cbScene.Get(), 0); }
    ic->PSSetConstantBuffers(0, 1, cbScene.GetAddressOf());
}

void Gpu::background(const SceneParams& p) {
    if (!ready || !hdr.rtv) return;
    begin(); sceneCB(p, W, H);
    ID3D11ShaderResourceView* nul[3] = {}; ic->PSSetShaderResources(0, 3, nul);
    ID3D11RenderTargetView* r = hdr.rtv.Get(); ic->OMSetRenderTargets(1, &r, nullptr);
    D3D11_VIEWPORT vp = { 0.f, 0.f, (float)W, (float)H, 0.f, 1.f }; ic->RSSetViewports(1, &vp);
    const float bf[4] = { 1, 1, 1, 1 }; ic->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
    ic->PSSetShader(psBg.Get(), nullptr, 0);
    ic->Draw(3, 0);
    ic->OMSetRenderTargets(0, nullptr, nullptr);
}

void Gpu::post(const PostParams& p) {
    if (!ready || !compBmp || !backRtv) return;
    begin();
    ic->PSSetConstantBuffers(0, 1, cb.GetAddressOf());

    CBData c = {};
    c.time = p.time; c.threshold = 0.04f; c.knee = 0.10f; c.thr2 = 0.35f;   // nearly all of the visual glows; the empty black does not
    c.streak = p.streak; c.grain = p.grain; c.grade = p.grade; c.vignette = p.vignette; c.caPx = p.ca;
    c.accent[0] = p.accent[0]; c.accent[1] = p.accent[1]; c.accent[2] = p.accent[2]; c.accent[3] = 1.f;
    for (int k = 0; k < 3; k++) c.tint[k] = p.accent[k] * 0.45f + (k == 0 ? 0.35f : (k == 1 ? 0.45f : 0.55f));   // cool, theme-tinted streaks
    c.tint[3] = 1.f;
    auto setSize = [&](const Target& src, int dw, int dh) { c.texel[0] = 1.f / src.w; c.texel[1] = 1.f / src.h; c.res[0] = (float)dw; c.res[1] = (float)dh; };
    const float z[4] = { 0, 0, 0, 1 };

    const bool doBloom = p.bloom > 0.01f, doStreak = p.streak > 0.01f;
    if (doBloom || doStreak) {
        const Target& src = scn.srv ? scn : hdr;               // glow comes from the visual alone (not the backdrop), like the classic look
        setSize(src, bloom[0].w, bloom[0].h);
        pass(psPrefilter.Get(), &bloom[0], nullptr, bloom[0].w, bloom[0].h, src.srv.Get(), nullptr, nullptr, &c, false, 1.f);
        for (int i = 1; i < LEVELS; i++) {
            setSize(bloom[i - 1], bloom[i].w, bloom[i].h);
            pass(psDown.Get(), &bloom[i], nullptr, bloom[i].w, bloom[i].h, bloom[i - 1].srv.Get(), nullptr, nullptr, &c, false, 1.f);
        }
        if (doStreak) {                                        // streaks grow out of the quarter-size level
            const float strides[3] = { 1.f, 4.f, 14.f };
            const Target* in = &bloom[1];
            for (int k = 0; k < 3; k++) {
                Target& dst = streak[k & 1];
                c.stride = strides[k]; c.thr2 = k == 0 ? 0.35f : 0.f;
                setSize(*in, dst.w, dst.h);
                pass(psStreak.Get(), &dst, nullptr, dst.w, dst.h, in->srv.Get(), nullptr, nullptr, &c, false, 1.f);
                in = &dst;
            }
        }
        else ic->ClearRenderTargetView(streak[0].rtv.Get(), z);
        if (doBloom) {
            c.stride = 0.6f + 0.5f * p.bloomSize;
            float scatter = clampv(0.55f + 0.12f * (p.bloomSize - 1.f), 0.35f, 0.85f);
            for (int i = LEVELS - 1; i >= 1; i--) {
                setSize(bloom[i], bloom[i - 1].w, bloom[i - 1].h);
                pass(psUp.Get(), &bloom[i - 1], nullptr, bloom[i - 1].w, bloom[i - 1].h, bloom[i].srv.Get(), nullptr, nullptr, &c, true, scatter);
            }
        }
        else ic->ClearRenderTargetView(bloom[0].rtv.Get(), z);
    }
    else { ic->ClearRenderTargetView(bloom[0].rtv.Get(), z); ic->ClearRenderTargetView(streak[0].rtv.Get(), z); }

    c.intensity = doBloom ? p.bloom * 0.62f : 0.f;           // about the same total glow as the classic two-blur bloom
    setSize(hdr, W, H);
    pass(psFinal.Get(), nullptr, backRtv.Get(), W, H, hdr.srv.Get(), bloom[0].srv.Get(), streak[0].srv.Get(), &c, false, 1.f);

    ID3D11ShaderResourceView* nul[3] = {};
    ic->PSSetShaderResources(0, 3, nul);
    ic->OMSetRenderTargets(0, nullptr, nullptr);
}
