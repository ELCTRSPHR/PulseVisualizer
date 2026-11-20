// Direct2D renderer: setup, frame composition, primitives
#include "common.h"
#include "util.h"
#include "settings.h"
#include "color.h"
#include "analyzer.h"
#include "spotify.h"
#include "ui_state.h"
#include "renderer.h"

bool Renderer::init(HWND h) {
    RECT rc; GetClientRect(h, &rc); W = std::max(1, (int)rc.right); H = std::max(1, (int)rc.bottom);
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &d3d, nullptr, nullptr))) return false;
    ComPtr<IDXGIDevice> dxgi; d3d.As(&dxgi);
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr, (void**)factory.GetAddressOf()))) return false;
    factory->CreateDevice(dxgi.Get(), &d2dDev);
    d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx);
    ctx->SetDpi(96.f, 96.f);
    rt = ctx.Get();

    ComPtr<IDXGIAdapter> ad; dxgi->GetAdapter(&ad);
    ComPtr<IDXGIFactory2> fac; ad->GetParent(IID_PPV_ARGS(&fac));
    DXGI_SWAP_CHAIN_DESC1 d = {};
    d.Width = W; d.Height = H; d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 2; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(fac->CreateSwapChainForHwnd(d3d.Get(), h, &d, nullptr, nullptr, &swap))) return false;
    fac->MakeWindowAssociation(h, DXGI_MWA_NO_ALT_ENTER);

    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &br);
    factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND), nullptr, 0, &roundStroke);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)dw.GetAddressOf());

    ctx->CreateEffect(CLSID_D2D1GaussianBlur, &blurS); ctx->CreateEffect(CLSID_D2D1GaussianBlur, &blurL);
    ctx->CreateEffect(CLSID_D2D1ColorMatrix, &matS);   ctx->CreateEffect(CLSID_D2D1ColorMatrix, &matL);
    ctx->CreateEffect(CLSID_D2D1ColorMatrix, &spR);    ctx->CreateEffect(CLSID_D2D1ColorMatrix, &spG);   ctx->CreateEffect(CLSID_D2D1ColorMatrix, &spB);
    ctx->CreateEffect(CLSID_D2D1GaussianBlur, &artBlur); ctx->CreateEffect(CLSID_D2D1ColorMatrix, &artMat);
    blurS->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    blurL->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    matS->SetInputEffect(0, blurS.Get()); matL->SetInputEffect(0, blurL.Get());
    spR->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, D2D1::Matrix5x4F(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0));
    spG->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, D2D1::Matrix5x4F(0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0));
    spB->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, D2D1::Matrix5x4F(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0));
    artBlur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 7.f);
    artMat->SetInputEffect(0, artBlur.Get());
    ctx->CreateEffect(CLSID_D2D1GaussianBlur, &artBlurP); ctx->CreateEffect(CLSID_D2D1ColorMatrix, &artMatP);
    artBlurP->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 7.f);
    ctx->CreateEffect(CLSID_D2D1GaussianBlur, &lyBlurS); ctx->CreateEffect(CLSID_D2D1GaussianBlur, &lyBlurL);
    ctx->CreateEffect(CLSID_D2D1ColorMatrix, &lyMatS);   ctx->CreateEffect(CLSID_D2D1ColorMatrix, &lyMatL);
    lyMatS->SetInputEffect(0, lyBlurS.Get()); lyMatL->SetInputEffect(0, lyBlurL.Get());
    if (SUCCEEDED(d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &rec))) rec->SetDpi(96.f, 96.f);
    artMatP->SetInputEffect(0, artBlurP.Get());
    gpu.init(d3d.Get());                                   // shader effects; the plain Direct2D path is used if this fails

    makeFonts(h);
    createTargets();
    baseY = H * 0.8f; maxHt = H * 0.6f;
    memset(hist, 0, sizeof(hist));
    return true;
}
void Renderer::resize(int w, int h) {
    if (w < 1 || h < 1 || !swap) return;
    W = w; H = h;
    ctx->SetTarget(nullptr); back.Reset(); scene.Reset(); gpu.release();
    blurS->SetInput(0, nullptr); blurL->SetInput(0, nullptr); spR->SetInput(0, nullptr); spG->SetInput(0, nullptr); spB->SetInput(0, nullptr);
    swap->ResizeBuffers(0, W, H, DXGI_FORMAT_UNKNOWN, 0);
    makeFonts(gHwnd);
    createTargets();
}
void Renderer::render(float dt) {
    uiDt = clampv(dt, 0.f, 0.1f);
    double now = nowSec();
    sc = std::min(W, H) / 1080.f;
    double sT = now - gSplashT0; bool splash = gSplashT0 > 0 && sT < gSplashDur;
    float sFade = splash ? smooth01((float)(gSplashDur - sT) / gSplashOut) : 0.f, sIn = smooth01((float)sT / 1.0f);
    if (!splash && gSplashT0 > 0 && !gSplashDone) { gSplashDone = true; toast(L"Press Tab for settings and shortcuts"); }
    theme = (int)(S.theme + 0.5f); byLevel = S.colorBy > 0.5f;
    gBright = S.bright; gHueOff = S.hue;
    phase += dt * S.colorSpeed * 0.12f * (1.f + an.bass * 2.f);
    rotAngle += dt * S.rot * (1.2f + an.bass * 3.f + an.pulse * 2.f);
    float punch = 1.f + an.pulse * 0.10f * S.punch;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) gAlbumCur[i][j] += (gAlbumTgt[i][j] - gAlbumCur[i][j]) * (1.f - expf(-dt * 3.f));

    { ArtData ad; if (gSp.getArt(ad, artGen) && ad.w > 0) uploadArt(ad); }
    { std::string n = gSp.takeNotice(); if (!n.empty()) toast(toW(n)); }
    { PlayerState p = gSp.state(); if (p.t.id != lastTrackId) { lastTrackId = p.t.id; if (!p.t.id.empty()) gTrackT = now; } }
    if (S.autoCycle > 0.5f) {
        cycleT += dt;
        if (cycleT > S.autoCycle) { cycleT = 0.f; S.mode = fmodf(S.mode + 1.f, 3.f); S.theme = fmodf(S.theme + 1.f, 8.f); }
    }
    else cycleT = 0.f;
    histAcc += dt; float iv = 1.f / (24.f * std::max(0.2f, S.flow)); int guard = 0;
    while (histAcc >= iv && guard++ < 4) { histAcc -= iv; histHead = (histHead + 1) % HIST_N; memcpy(hist[histHead], an.val, sizeof(an.val)); }
    updateParticles(dt);
    artR = 0.f;
    if (gSp.getLyrics(lyr, lyrGen)) { lyrAt = now; buildLyricRows(); lyInit = true; }
    {   // Radial + lyrics: slide the vinyl left and give the lyrics the right-hand column
        bool avail = S.lyrics > 0.5f && lyr.state == 1 && !lyr.lines.empty() && lyr.id == lastTrackId;
        bool introUp = introActive() && (now - gTrackT) >= 0.0 && (now - gTrackT) < 2.9;   // wait for the "NOW PLAYING" card to leave
        bool wantSide = avail && !introUp && S.lyricSide > 0.5f && (int)(S.mode + 0.5f) != 0;      // Bars always uses the centered top layout
        bool wantShift = wantSide && (int)(S.mode + 0.5f) >= 1;      // Radial and NCS move their circle out of the way
        float k = 1.f - expf(-dt * 6.f);
        lySide = wantSide;
        sideT += ((wantSide ? 1.f : 0.f) - sideT) * k;
        radShift += ((wantShift ? 1.f : 0.f) - radShift) * k;
    }
    prepareLyrics(dt, now);
    {   // glide the visual weight toward the selected mode
        int md = clampv((int)(S.mode + 0.5f), 0, 2);
        for (int i = 0; i < 3; i++) {
            float tg = (i == md) ? 1.f : 0.f;
            if (mixW[0] < 0.f) { for (int j = 0; j < 3; j++) mixW[j] = (j == md) ? 1.f : 0.f; break; }
            mixW[i] += (tg - mixW[i]) * (1.f - expf(-dt * 5.f));
            if (fabsf(tg - mixW[i]) < 0.004f) mixW[i] = tg;
        }
    }

    // ---------------- pass 1: scene (float buffer, additive, trails) ----------------
    ctx->SetTarget(scene.Get()); ctx->BeginDraw();
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    ctx->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    float keep = S.trails > 0.001f ? powf(S.trails, dt * 60.f) : 0.f;
    br->SetColor(D2D1::ColorF(0, 0, 0, 1.f - keep));
    rt->FillRectangle(D2D1::RectF(0, 0, (float)W, (float)H), br.Get());
    ctx->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
    {   // switching Bars <-> Radial: both visuals are drawn while their weights cross-fade
        float wB = smooth01(mixW[0]), wR = smooth01(mixW[1]), wN = smooth01(mixW[2]);
        if (wB > 0.005f) { visGain = wB; drawBars(punch); }
        if (wR > 0.005f) { visGain = wR; drawRadial(punch); }
        if (wN > 0.005f) { visGain = wN; drawNCS(punch); }
        visGain = 1.f;
    }
    if (S.particles > 0.5f) drawParticles();
    if (splash) {                                          // splash: dim the visual, then draw the logo additively so it blooms
        ctx->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
        br->SetColor(D2D1::ColorF(0, 0, 0, sFade)); rt->FillRectangle(D2D1::RectF(0, 0, (float)W, (float)H), br.Get());
        ctx->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        drawLogo(W * 0.5f, H * 0.42f, std::min(W, H) * 0.17f * (1.f + 0.12f * (1.f - sFade)), (float)sT, sIn * sFade);
    }
    ctx->EndDraw();

    // ---------------- pass 2: composite + bloom + UI ----------------
    // Shader effects on: the world layer (background, backdrop, scene) goes into an FP16 target, the GPU post pass
    // (bloom, streaks, aberration, tone map, grade, vignette, grain) writes the back buffer, then D2D adds disc, lyrics and UI.
    const bool fx = S.fx > 0.5f && gpu.ok() && gpu.comp();
    bgShader = fx && S.bgStyle > 0.5f && S.bgStrength > 0.01f;
    if (bgShader) {                                        // music-reactive shader background fills the world layer first
        SceneParams bp; fillSceneColors(bp);
        bp.style = (int)(S.bgStyle + 0.5f); bp.strength = S.bgStrength * (splash ? 1.f - sFade : 1.f);
        bp.time = (float)fmod(now, 1000.0); bp.bass = an.bass; bp.mid = an.mid; bp.treble = an.treble; bp.pulse = an.pulse; bp.level = an.level;
        gpu.background(bp);
    }
    ctx->SetTarget(fx ? (ID2D1Image*)gpu.comp() : (ID2D1Image*)back.Get()); ctx->BeginDraw();
    ctx->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    if (!bgShader) ctx->Clear(D2D1::ColorF(0.008f, 0.008f, 0.014f, 1.f));
    if (S.bgPulse > 0.001f) {
        D2D1_COLOR_F c0 = palette(theme, 0.5f, phase, S.bgPulse * (0.05f + 0.22f * an.bass + 0.30f * an.pulse));
        D2D1_COLOR_F c1 = c0; c1.a = 0.f;
        D2D1_GRADIENT_STOP gs[2] = { { 0.f, c0 },{ 1.f, c1 } };
        ComPtr<ID2D1GradientStopCollection> coll; ComPtr<ID2D1RadialGradientBrush> rb;
        rt->CreateGradientStopCollection(gs, 2, &coll);
        rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(W * 0.5f, H * 0.5f), D2D1::Point2F(0, 0), W * 0.6f, H * 0.7f), coll.Get(), &rb);
        rt->FillRectangle(D2D1::RectF(0, 0, (float)W, (float)H), rb.Get());
    }
    if (artPrev) { artFade += dt / 1.3f; if (artFade >= 1.f) { artFade = 1.f; artPrev.Reset(); artBrushP.Reset(); artBlurP->SetInput(0, nullptr); } }
    if ((art || artPrev) && S.artBg > 0.01f) drawArtBackdrop(dt);       // blurred, drifting, beat-reactive cover backdrop
    {   // track change: soft flash of the new cover's color
        float ta = (float)(now - gTrackT);
        if (!splash && ta >= 0.f && ta < 1.f) {
            float f = 1.f - ta; f *= f;
            br->SetColor(D2D1::ColorF(gAlbumCur[1][0], gAlbumCur[1][1], gAlbumCur[1][2], 0.16f * f));
            rt->FillRectangle(D2D1::RectF(0, 0, (float)W, (float)H), br.Get());
        }
    }
    if (splash) { br->SetColor(D2D1::ColorF(0, 0, 0, sFade)); rt->FillRectangle(D2D1::RectF(0, 0, (float)W, (float)H), br.Get()); }
    float shk = S.shake * an.pulse * 10.f * sc, shX = (frand() - 0.5f) * 2.f * shk, shY = (frand() - 0.5f) * 2.f * shk;
    float bloomK = 1.f - 0.60f * smooth01(mixW[0]) - 0.50f * smooth01(mixW[2]);   // NCS: a big solid ring blooms hard; Bars: dense, wide shapes would bloom to pure white
    if (S.split > 0.01f && !fx) {                          // chromatic aberration: R/G/B drawn with different offsets (shader does it radially when fx is on)
        float d = S.split * (1.5f + 9.f * an.pulse + 4.f * an.bass) * sc;
        D2D1_POINT_2F oR = D2D1::Point2F(shX - d, shY), oG = D2D1::Point2F(shX, shY), oB = D2D1::Point2F(shX + d, shY);
        ctx->DrawImage(spR.Get(), &oR, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_PLUS);
        ctx->DrawImage(spG.Get(), &oG, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_PLUS);
        ctx->DrawImage(spB.Get(), &oB, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_PLUS);
    }
    else {
        D2D1_POINT_2F o = D2D1::Point2F(shX, shY);
        ctx->DrawImage(scene.Get(), &o, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_PLUS);
    }
    if (S.bloom > 0.01f && !fx) {
        float bsz = S.bloomSize * sc;
        blurS->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 3.5f * bsz + 0.5f);
        blurL->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 20.f * bsz + 1.f);
        float k1 = S.bloom * 0.85f * bloomK, k2 = S.bloom * 0.70f * bloomK;
        matS->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, D2D1::Matrix5x4F(k1, 0, 0, 0, 0, k1, 0, 0, 0, 0, k1, 0, 0, 0, 0, 1, 0, 0, 0, 0));
        matL->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, D2D1::Matrix5x4F(k2, 0, 0, 0, 0, k2, 0, 0, 0, 0, k2, 0, 0, 0, 0, 1, 0, 0, 0, 0));
        ctx->DrawImage(matS.Get(), nullptr, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_PLUS);
        ctx->DrawImage(matL.Get(), nullptr, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_PLUS);
    }
    if (fx) {                                              // GPU post pass: world layer -> back buffer
        ctx->EndDraw();
        PostParams pp;
        pp.bloom = S.bloom * bloomK; pp.bloomSize = S.bloomSize;
        pp.streak = S.streaks * (0.6f + 0.6f * an.pulse) * bloomK;
        pp.ca = S.split * (1.5f + 9.f * an.pulse + 4.f * an.bass) * sc;
        pp.grain = S.grain; pp.grade = S.grade; pp.vignette = S.vignette; pp.time = (float)fmod(now, 1000.0);
        D2D1_COLOR_F acc = palette(theme, 0.5f, phase); pp.accent[0] = acc.r; pp.accent[1] = acc.g; pp.accent[2] = acc.b;
        gpu.post(pp);
        ctx->SetTarget(back.Get()); ctx->BeginDraw();
    }
    ctx->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    if (splash) {                                          // album disc only fades in as the splash fades out
        float oa = 1.f - sFade;
        if (oa > 0.01f) { ctx->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), oa), nullptr); drawCenterArt(); ctx->PopLayer(); }
    }
    else drawCenterArt();
    if (!splash && S.flash > 0.01f && an.pulse > 0.01f) {
        br->SetColor(palette(theme, 0.3f, phase, S.flash * an.pulse * 0.22f));
        rt->FillRectangle(D2D1::RectF(0, 0, (float)W, (float)H), br.Get());
    }
    if (S.vignette > 0.01f && !fx) {
        D2D1_GRADIENT_STOP gs[3] = { { 0.f, D2D1::ColorF(0,0,0,0) },{ 0.55f, D2D1::ColorF(0,0,0,0) },{ 1.f, D2D1::ColorF(0,0,0,clampv(S.vignette * 0.95f, 0.f, 1.f)) } };
        ComPtr<ID2D1GradientStopCollection> coll; ComPtr<ID2D1RadialGradientBrush> rb;
        rt->CreateGradientStopCollection(gs, 3, &coll);
        float rr = sqrtf((float)W * W + (float)H * H) * 0.5f;
        rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(W * 0.5f, H * 0.5f), D2D1::Point2F(0, 0), rr, rr), coll.Get(), &rb);
        rt->FillRectangle(D2D1::RectF(0, 0, (float)W, (float)H), rb.Get());
    }
    if (splash) drawSplashText((float)sT, sFade);
    else {
        // lyrics wait for the song intro: they fade in as the "NOW PLAYING" card slides away
        float lyGate = 1.f;
        if (introActive()) { float ta = (float)(now - gTrackT); if (ta >= 0.f && ta < 5.f) lyGate = smooth01((ta - 2.9f) / 0.55f); }
        rt->SetTransform(D2D1::Matrix3x2F::Identity());
        if (lyGate > 0.999f) drawLyrics();
        else if (lyGate > 0.01f) {                         // the visual's slide carries the motion; lyrics just fade
            ctx->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), lyGate), nullptr);
            drawLyrics();
            ctx->PopLayer();
        }
        drawUI(now);
    }
    ctx->EndDraw();
    swap->Present(1, 0);
}
void Renderer::createTargets() {
    ComPtr<IDXGISurface> surf; swap->GetBuffer(0, IID_PPV_ARGS(&surf));
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.f, 96.f);
    ctx->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &back);
    surf.Reset();
    gpu.resize(W, H, ctx.Get(), swap.Get());
    if (gpu.sceneBmp()) scene = gpu.sceneBmp();            // shader-readable, so bloom can glow from the visual alone
    else {
        D2D1_BITMAP_PROPERTIES1 sp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_R16G16B16A16_FLOAT, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
        ctx->CreateBitmap(D2D1::SizeU(W, H), nullptr, 0, sp, &scene);
    }
    blurS->SetInput(0, scene.Get()); blurL->SetInput(0, scene.Get());
    spR->SetInput(0, scene.Get()); spG->SetInput(0, scene.Get()); spB->SetInput(0, scene.Get());
    ctx->SetTarget(scene.Get()); ctx->BeginDraw(); ctx->Clear(D2D1::ColorF(0, 0, 0, 1)); ctx->EndDraw();
    ctx->SetTarget(nullptr);
}
void Renderer::makeFonts(HWND h) {
    UINT dpi = GetDpiForWindow(h); ui = dpi ? dpi / 96.f : 1.f;
    fL.Reset(); fR.Reset(); fTitle.Reset(); fSmall.Reset(); fMed.Reset(); fLy.Reset(); fBtn.Reset(); fIntro1.Reset(); fIntro2.Reset();
    auto mk = [&](ComPtr<IDWriteTextFormat>& f, float size, DWRITE_FONT_WEIGHT w, DWRITE_TEXT_ALIGNMENT al) {
        dw->CreateTextFormat(L"Segoe UI", nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size * ui, L"en-us", &f);
        f->SetTextAlignment(al); f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING tr = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        ComPtr<IDWriteInlineObject> ell; dw->CreateEllipsisTrimmingSign(f.Get(), &ell); f->SetTrimming(&tr, ell.Get());
        };
    mk(fL, 14.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
    mk(fR, 14.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING);
    mk(fTitle, 18.f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    mk(fSmall, 12.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
    mk(fMed, 15.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    mk(fBtn, 14.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    mk(fIntro1, 46.f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    mk(fIntro2, 20.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    mk(fSp1, 60.f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    mk(fSp2, 17.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    makeLyricFont();
}
void Renderer::uploadArt(const ArtData& ad) {
    if (art) {                                           // keep the old cover so the new one can cross-fade over it
        artPrev = art; artPrevW = artW; artPrevH = artH; artBrushP = artBrush;
        artBlurP->SetInput(0, artPrev.Get()); artFade = 0.f;
    }
    art.Reset(); artBrush.Reset();
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
    if (FAILED(ctx->CreateBitmap(D2D1::SizeU(ad.w, ad.h), ad.px.data(), ad.w * 4, bp, &art))) { art.Reset(); return; }
    artW = ad.w; artH = ad.h;
    rt->CreateBitmapBrush(art.Get(), &artBrush);
    artBlur->SetInput(0, art.Get());
    memcpy(gAlbumTgt, ad.col, sizeof(gAlbumTgt));
}
// ---- drawing helpers ----

void Renderer::fillSceneColors(SceneParams& p) {
    D2D1_COLOR_F a = palette(theme, 0.1f, phase), b = palette(theme, 0.5f, phase), c = palette(theme, 0.9f, phase);
    p.c0[0] = a.r; p.c0[1] = a.g; p.c0[2] = a.b;
    p.c1[0] = b.r; p.c1[1] = b.g; p.c1[2] = b.b;
    p.c2[0] = c.r; p.c2[1] = c.g; p.c2[2] = c.b;
}

void Renderer::setc(D2D1_COLOR_F c, float bright, float alpha) { br->SetColor(D2D1::ColorF(c.r * bright, c.g * bright, c.b * bright, c.a * alpha * visGain)); }
void Renderer::fillRR(float x0, float y0, float x1, float y1, float r) { rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x0, y0, x1, y1), r, r), br.Get()); }
ComPtr<ID2D1PathGeometry> Renderer::path(const std::vector<D2D1_POINT_2F>& p, bool fill, float closeY) {
    ComPtr<ID2D1PathGeometry> g; factory->CreatePathGeometry(&g);
    ComPtr<ID2D1GeometrySink> s; g->Open(&s);
    if (fill) { s->BeginFigure(D2D1::Point2F(p.front().x, closeY), D2D1_FIGURE_BEGIN_FILLED); s->AddLine(p.front()); }
    else s->BeginFigure(p.front(), D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < p.size(); i++) s->AddLine(p[i]);
    if (fill) { s->AddLine(D2D1::Point2F(p.back().x, closeY)); s->EndFigure(D2D1_FIGURE_END_CLOSED); }
    else s->EndFigure(D2D1_FIGURE_END_OPEN);
    s->Close(); return g;
}
void Renderer::fillPoly(const std::vector<D2D1_POINT_2F>& p) {
    ComPtr<ID2D1PathGeometry> g; factory->CreatePathGeometry(&g);
    ComPtr<ID2D1GeometrySink> s; g->Open(&s);
    s->BeginFigure(p[0], D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < p.size(); i++) s->AddLine(p[i]);
    s->EndFigure(D2D1_FIGURE_END_CLOSED); s->Close(); rt->FillGeometry(g.Get(), br.Get());
}
ComPtr<ID2D1LinearGradientBrush> Renderer::gradBrush(D2D1_COLOR_F a, D2D1_COLOR_F b, float x1, float y1, float x2, float y2, float alpha) {
    D2D1_GRADIENT_STOP st[2] = { { 0.f, D2D1::ColorF(a.r, a.g, a.b, alpha) },{ 1.f, D2D1::ColorF(b.r, b.g, b.b, alpha) } };
    ComPtr<ID2D1GradientStopCollection> coll; ComPtr<ID2D1LinearGradientBrush> g;
    rt->CreateGradientStopCollection(st, 2, &coll);
    rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2)), coll.Get(), &g);
    return g;
}
Renderer gR;