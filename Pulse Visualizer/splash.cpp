// Renderer: startup splash and logo
#include "common.h"
#include "settings.h"
#include "color.h"
#include "ui_state.h"
#include "renderer.h"

    // ---- logo: ring of rainbow spectrum bars around a heartbeat line ----

void Renderer::drawLogo(float cx, float cy, float R, float t, float a) {
    if (a <= 0.003f) return;
    const int N = 56; float grow = clampv(t / 2.0f, 0.f, 1.f); grow = 1.f - powf(1.f - grow, 4.f);
    float pulse = 0.5f + 0.5f * sinf(t * 5.2f), r0 = R * 0.62f * (1.f + 0.03f * pulse);
    {   // soft colored halo behind everything
        D2D1_COLOR_F g0 = hsv(t * 0.08f + 0.62f, 0.8f, 1.f, 0.30f * a), g1 = g0; g1.a = 0.f;
        D2D1_GRADIENT_STOP gs[2] = { { 0.f, g0 },{ 1.f, g1 } };
        ComPtr<ID2D1GradientStopCollection> coll; ComPtr<ID2D1RadialGradientBrush> rb;
        rt->CreateGradientStopCollection(gs, 2, &coll);
        rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), R * 2.f, R * 2.f), coll.Get(), &rb);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), R * 2.f, R * 2.f), rb.Get());
    }
    float lw = std::max(2.f, 2.f * PI * r0 / N * 0.55f);
    for (int i = 0; i < N; i++) {
        float u = (float)i / N, ang = u * 2.f * PI - PI * 0.5f + t * 0.25f;
        float wv = 0.5f + 0.5f * sinf(u * 2.f * PI * 3.f + t * 3.1f) * cosf(u * 2.f * PI * 2.f - t * 1.7f);
        float sweep = clampv(grow * 1.6f - u * 0.6f, 0.f, 1.f);
        float len = R * (0.10f + 0.42f * (0.35f + 0.65f * wv)) * sweep + 2.f * sc;
        D2D1_COLOR_F c = hsv(u * 0.83f + t * 0.08f, 0.9f, 1.f, 1.f);
        float dx = cosf(ang), dy = sinf(ang);
        setc(c, 0.95f, a);
        rt->DrawLine(D2D1::Point2F(cx + dx * r0, cy + dy * r0), D2D1::Point2F(cx + dx * (r0 + len), cy + dy * (r0 + len)), br.Get(), lw, roundStroke.Get());
        setc(c, 0.55f, a);
        rt->DrawLine(D2D1::Point2F(cx + dx * (r0 - lw * 1.6f), cy + dy * (r0 - lw * 1.6f)), D2D1::Point2F(cx + dx * (r0 - lw * 1.6f - len * 0.28f), cy + dy * (r0 - lw * 1.6f - len * 0.28f)), br.Get(), lw, roundStroke.Get());
    }
    br->SetColor(D2D1::ColorF(1, 1, 1, 0.75f * a * grow));
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r0 * 0.80f, r0 * 0.80f), br.Get(), 2.2f * sc, roundStroke.Get());
    br->SetColor(D2D1::ColorF(1, 1, 1, 0.25f * a * grow));
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r0 * 0.66f, r0 * 0.66f), br.Get(), 1.2f * sc, roundStroke.Get());
    // heartbeat line, revealed left to right
    static const float hx[] = { -1.f, -0.45f, -0.33f, -0.22f, -0.12f, -0.04f, 0.06f, 0.16f, 0.24f, 0.4f, 0.5f, 0.6f, 1.f };
    static const float hy[] = { 0.f, 0.f, 0.12f, 0.f, 0.f, -0.55f, 0.7f, -0.18f, 0.f, 0.f, 0.1f, 0.f, 0.f };
    std::vector<D2D1_POINT_2F> pts;
    for (int i = 0; i < 13; i++) pts.push_back(D2D1::Point2F(cx + hx[i] * r0 * 0.62f, cy + hy[i] * R * 0.34f));
    float rev = smooth01((t - 0.9f) / 1.6f), xl = cx - r0 * 0.62f - 6.f * sc, xr = cx + r0 * 0.62f + 6.f * sc;
    rt->PushAxisAlignedClip(D2D1::RectF(xl, cy - R, xl + (xr - xl) * rev, cy + R), D2D1_ANTIALIAS_MODE_ALIASED);
    br->SetColor(D2D1::ColorF(1, 1, 1, a * (0.8f + 0.2f * pulse)));
    rt->DrawGeometry(path(pts, false, 0.f).Get(), br.Get(), 3.2f * sc, roundStroke.Get());
    rt->PopAxisAlignedClip();
}
void Renderer::drawSplashText(float t, float fade) {
    float R = std::min(W, H) * 0.17f, cx = W * 0.5f, y1 = H * 0.42f + R * 1.55f;
    float ta = smooth01((t - 1.6f) / 1.1f) * fade;
    if (ta > 0.f) {
        text(L"PULSE", 0, y1, (float)W, 84 * ui, fSp1.Get(), D2D1::ColorF(1, 1, 1, ta));
        text(L"V I S U A L I Z E R", 0, y1 + 80 * ui, (float)W, 28 * ui, fSp2.Get(), D2D1::ColorF(0.78f, 0.88f, 1.f, 0.75f * ta));
    }
    float lp = smooth01((t - 2.3f) / 1.3f);
    if (lp > 0.f && fade > 0.f) {
        float half = 140 * ui * lp, yy = y1 + 118 * ui;
        ComPtr<ID2D1LinearGradientBrush> gb = gradBrush(hsv(0.92f + t * 0.05f, 0.85f, 1.f, 1.f), hsv(0.55f + t * 0.05f, 0.85f, 1.f, 1.f), cx - half, yy, cx + half, yy, 0.9f * fade);
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - half, yy, cx + half, yy + 3 * ui), 1.5f * ui, 1.5f * ui), gb.Get());
    }
}