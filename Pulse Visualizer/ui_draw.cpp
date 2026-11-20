// Renderer: HUD, settings panel, now-playing card
#include "common.h"
#include "util.h"
#include "http.h"
#include "settings.h"
#include "color.h"
#include "analyzer.h"
#include "spotify.h"
#include "ui_state.h"
#include "renderer.h"

    // ---- text & hit-testing helpers ----

void Renderer::text(const std::wstring& s, float x, float y, float w, float h, IDWriteTextFormat* f, D2D1_COLOR_F c) {
    br->SetColor(c); rt->DrawText(s.c_str(), (UINT32)s.size(), f, D2D1::RectF(x, y, x + w, y + h), br.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Renderer::addHit(int id, float x0, float y0, float x1, float y1) {
    Hit h; h.x0 = hitPx + (x0 - hitPx) * hitK; h.x1 = hitPx + (x1 - hitPx) * hitK;
    h.y0 = hitPy + (y0 - hitPy) * hitK; h.y1 = hitPy + (y1 - hitPy) * hitK; h.id = id; gHits.push_back(h);
}
void Renderer::fieldInset(float x, float y, float w, float h, float r, D2D1_COLOR_F ac, float focus) {      // a recessed well (text fields)
    br->SetColor(D2D1::ColorF(0.f, 0.f, 0.02f, 0.30f)); fillRR(x, y, x + w, y + h, r);
    {
        D2D1_GRADIENT_STOP st[2] = { { 0.f, D2D1::ColorF(0, 0, 0, 0.30f) },{ 1.f, D2D1::ColorF(0, 0, 0, 0.f) } };    // inner shadow from the top edge
        ComPtr<ID2D1GradientStopCollection> coll; ComPtr<ID2D1LinearGradientBrush> lb;
        rt->CreateGradientStopCollection(st, 2, &coll);
        rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(x, y), D2D1::Point2F(x, y + h * 0.6f)), coll.Get(), &lb);
        if (lb) rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r), lb.Get());
    }
    br->SetColor(D2D1::ColorF(1, 1, 1, 0.10f)); rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r), br.Get(), 1.f * ui);
    if (focus > 0.f) { br->SetColor(D2D1::ColorF(ac.r, ac.g, ac.b, 0.55f * focus)); rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r), br.Get(), 1.4f * ui); }
}
std::wstring Renderer::valStr(const Param& p) {
    wchar_t b[48];
    if (p.labels) swprintf_s(b, L"%ls", p.labels[(int)(*p.v + 0.5f)]);
    else if (p.v == &S.autoCycle && *p.v < 0.5f) swprintf_s(b, L"Off");
    else if (p.st >= 1.f) swprintf_s(b, L"%d", (int)(*p.v + 0.5f));
    else swprintf_s(b, L"%.2f", *p.v);
    return b;
}
// ---- UI root ----

void Renderer::drawUI(double now) {
    gHits.clear();
    D2D1_COLOR_F accent = palette(theme, 0.5f, phase);

    if (gShowUI) {
        drawIntro(now, accent);
        drawPanel(accent);
        drawNowPlaying(now, accent);
    }
    if (gOverlay == OV_SETUP) drawSetup(now, accent);

    float tAge = (float)(now - gToastT);
    float ta = std::min(clampv(1.f - (tAge - 1.8f) / 0.6f, 0.f, 1.f), smooth01(tAge / 0.18f));   // quick fade in, gentle fade out
    if (ta > 0.f && !gToastText.empty()) {
        // a small, quiet pill near the top: accent dot + one line of text, sized to the text
        float txtW = 0.f;
        {
            ComPtr<IDWriteTextLayout> tl;
            if (SUCCEEDED(dw->CreateTextLayout(gToastText.c_str(), (UINT32)gToastText.size(), fL.Get(), 4000.f, 100.f, &tl)) && tl) { DWRITE_TEXT_METRICS tm = {}; tl->GetMetrics(&tm); txtW = tm.widthIncludingTrailingWhitespace; }
        }
        const float th = 30 * ui, padL = 30 * ui, padR = 16 * ui;
        float tw = std::min((float)W - 40 * ui, txtW + padL + padR), x = (W - tw) * 0.5f, y = 18 * ui + (1.f - ta) * -8 * ui;
        ctx->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), ta), nullptr);
        drawPane(x, y, tw, th, th * 0.5f, accent, 1.0f);
        D2D1_COLOR_F dc = whiten(accent, 0.3f);
        br->SetColor(D2D1::ColorF(dc.r, dc.g, dc.b, 0.25f)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 16 * ui, y + th * 0.5f), 6 * ui, 6 * ui), br.Get());
        br->SetColor(D2D1::ColorF(dc.r, dc.g, dc.b, 1.f));    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 16 * ui, y + th * 0.5f), 3 * ui, 3 * ui), br.Get());
        text(gToastText, x + padL, y, tw - padL - padR + 2 * ui, th, fL.Get(), D2D1::ColorF(1, 1, 1, 0.92f));
        ctx->PopLayer();
    }
}
// ---- song intro: a lower-left title card that reveals itself when a new song starts ----
bool Renderer::introActive() const {
    if (S.intro < 0.5f || !fIntro1 || !fIntro2 || !gShowUI || gSp.status != Spotify::ST_ON || gOverlay != OV_NONE) return false;
    return !gSp.state().t.name.empty();
}
void Renderer::drawIntro(double now, D2D1_COLOR_F ac) {
    if (!introActive()) return;
    PlayerState s = gSp.state();
    const float ta = (float)(now - gTrackT);
    const float total = 3.5f; if (ta < 0.f || ta > total) return;
    auto seg = [&](float a, float b) { return clampv((ta - a) / (b - a), 0.f, 1.f); };
    auto eOut = [](float x) { return 1.f - (1.f - x) * (1.f - x) * (1.f - x); };
    const float out = smooth01(seg(2.85f, total)), fade = 1.f - out, slide = -36.f * ui * out * out;
    const float x0 = 56.f * ui + slide, maxW = std::min((float)W * 0.55f, 900.f * ui);
    const float tH = 58.f * ui, aH = 28.f * ui, lH = 18.f * ui;
    const float yArtist = (float)H - 250.f * ui, yTitle = yArtist - tH, yLabel = yTitle - lH - 4.f * ui;
    std::wstring title = toW(s.t.name), artist = toW(s.t.artist);
    D2D1_COLOR_F lab = whiten(ac, 0.45f);

    // accent bar grows down beside the text
    float bar = eOut(seg(0.05f, 0.55f));
    br->SetColor(D2D1::ColorF(ac.r, ac.g, ac.b, 0.95f * fade));
    fillRR(x0 - 16.f * ui, yLabel + 2.f * ui, x0 - 11.f * ui, yLabel + 2.f * ui + (yArtist + aH - yLabel - 4.f * ui) * bar, 2.5f * ui);
    br->SetColor(D2D1::ColorF(ac.r, ac.g, ac.b, 0.25f * fade));
    fillRR(x0 - 20.f * ui, yLabel - 2.f * ui, x0 - 7.f * ui, yLabel + 6.f * ui + (yArtist + aH - yLabel - 4.f * ui) * bar, 6.f * ui);

    auto reveal = [&](const std::wstring& str, float y, float h, IDWriteTextFormat* f, D2D1_COLOR_F c, float k) {
        if (k <= 0.f) return;
        float e = eOut(k), oy = (1.f - e) * h * 0.9f;                       // slides up out of a mask
        rt->PushAxisAlignedClip(D2D1::RectF(x0 - 2.f, y, x0 + maxW + 4.f, y + h), D2D1_ANTIALIAS_MODE_ALIASED);
        D2D1_COLOR_F sh = D2D1::ColorF(0, 0, 0, 0.55f * c.a);
        text(str, x0 + 2.f * ui, y + oy + 2.f * ui, maxW, h, f, sh);        // soft drop shadow for legibility on any visual
        text(str, x0, y + oy, maxW, h, f, c);
        rt->PopAxisAlignedClip();
        };
    reveal(L"N O W   P L A Y I N G", yLabel, lH, fSmall.Get(), D2D1::ColorF(lab.r, lab.g, lab.b, 0.9f * fade), seg(0.10f, 0.50f));
    reveal(title, yTitle, tH, fIntro1.Get(), D2D1::ColorF(1, 1, 1, fade), seg(0.18f, 0.70f));
    reveal(artist, yArtist, aH, fIntro2.Get(), D2D1::ColorF(1, 1, 1, 0.72f * fade), seg(0.32f, 0.84f));
}
// ---- settings panel: tabs, clean rows with inline sliders, and a shortcuts page ----

bool Renderer::hov(float x0, float y0, float x1, float y1) const {
    return gPanel && gMouseX >= x0 && gMouseX <= x1 && gMouseY >= y0 && gMouseY <= y1;
}
bool Renderer::hovS(float x0, float y0, float x1, float y1) const {
    float sx0 = hitPx + (x0 - hitPx) * hitK, sx1 = hitPx + (x1 - hitPx) * hitK, sy0 = hitPy + (y0 - hitPy) * hitK, sy1 = hitPy + (y1 - hitPy) * hitK;
    return gMouseX >= sx0 && gMouseX <= sx1 && gMouseY >= sy0 && gMouseY <= sy1;
}
float Renderer::hoverAnim(int key, bool on) {
    float& v = hovAmt[key & 15]; v += ((on ? 1.f : 0.f) - v) * (1.f - expf(-uiDt * 14.f)); return v;
}
void Renderer::arrowTri(float cx, float cy, float s, int dir, D2D1_COLOR_F c) {
    br->SetColor(c);
    std::vector<D2D1_POINT_2F> p;
    if (dir < 0) { p.push_back(D2D1::Point2F(cx + s * 0.6f, cy - s)); p.push_back(D2D1::Point2F(cx + s * 0.6f, cy + s)); p.push_back(D2D1::Point2F(cx - s * 0.8f, cy)); }
    else { p.push_back(D2D1::Point2F(cx - s * 0.6f, cy - s)); p.push_back(D2D1::Point2F(cx - s * 0.6f, cy + s)); p.push_back(D2D1::Point2F(cx + s * 0.8f, cy)); }
    fillPoly(p);
}
void Renderer::button(const std::wstring& label, float x, float y, float w, float h, int id, D2D1_COLOR_F ac, bool primary, bool enabled, float alpha) {
    bool hv = enabled && hov(x, y, x + w, y + h);
    float fa = !enabled ? 0.04f : (primary ? 0.30f : 0.09f) + (hv ? 0.16f : 0.f);
    float tr = primary ? ac.r : 1.f, tg = primary ? ac.g : 1.f, tb = primary ? ac.b : 1.f;
    br->SetColor(D2D1::ColorF(tr, tg, tb, fa * alpha)); fillRR(x, y, x + w, y + h, h * 0.5f);
    br->SetColor(D2D1::ColorF(tr, tg, tb, (hv ? 0.75f : (enabled ? 0.30f : 0.12f)) * alpha));
    rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), h * 0.5f, h * 0.5f), br.Get(), 1.f * ui);
    text(label, x, y, w, h, fBtn.Get(), D2D1::ColorF(1, 1, 1, (!enabled ? 0.35f : (hv ? 1.f : 0.88f)) * alpha));
    if (id && enabled && gPanel) addHit(id, x, y, x + w, y + h);
}
void Renderer::drawPresetsTab(float x, float y, float w, D2D1_COLOR_F ac, float alpha) {
    const float u = ui, bh = 30 * u, gap = 8 * u;
    D2D1_COLOR_F hd = whiten(ac, 0.6f); hd.a = 0.8f * alpha;
    text(NPRESET > 1 ? L"BUILT-IN LOOKS" : L"BUILT-IN LOOK", x, y, w, 22 * u, fSmall.Get(), hd);
    y += 26 * u;
    const int rows = (NPRESET + 2) / 3;
    float bw = (w - 2 * gap) / 3.f;
    for (int i = 0; i < NPRESET; i++)
        button(kPresetNames[i], x + (i % 3) * (bw + gap), y + (i / 3) * (bh + gap), bw, bh, H_PACT + i, ac, false, true, alpha);
    y += rows * bh + (rows - 1) * gap + 18 * u;
    text(L"YOUR SAVED LOOKS", x, y, w, 22 * u, fSmall.Get(), hd);
    y += 26 * u;
    for (int i = 0; i < 4; i++) {
        float ry = y + i * (bh + 6 * u), bw2 = 84 * u;
        D2D1_COLOR_F sc2 = gSlotUsed[i] ? whiten(ac, 0.6f) : D2D1::ColorF(1, 1, 1, 0.35f); sc2.a *= alpha;
        text(L"Slot " + std::to_wstring(i + 1), x + 6 * u, ry, 80 * u, bh, fL.Get(), D2D1::ColorF(1, 1, 1, 0.85f * alpha));
        text(gSlotUsed[i] ? L"Saved" : L"Empty", x + 90 * u, ry, 80 * u, bh, fSmall.Get(), sc2);
        button(L"Load", x + w - 2 * bw2 - gap, ry, bw2, bh, H_PACT + 10 + i, ac, false, gSlotUsed[i], alpha);
        button(L"Save", x + w - bw2, ry, bw2, bh, H_PACT + 20 + i, ac, true, true, alpha);
    }
    y += 4 * (bh + 6 * u) + 12 * u;
    text(L"SHARE YOUR LOOK", x, y, w, 22 * u, fSmall.Get(), hd);
    y += 26 * u;
    float hw = (w - gap) * 0.5f;
    button(L"Copy look code", x, y, hw, bh, H_PACT + 30, ac, true, true, alpha);
    button(L"Paste look code", x + hw + gap, y, hw, bh, H_PACT + 31, ac, false, true, alpha);
    text(L"Copies every setting as one short code - friends paste it in.", x, y + bh + 4 * u, w, 22 * u, fSmall.Get(), D2D1::ColorF(1, 1, 1, 0.42f * alpha));
}
void Renderer::drawPanel(D2D1_COLOR_F accent) {
    const int NT = 7;
    if (gPanel) panelT = std::min(panelT + uiDt, 1.f); else panelT = std::max(panelT - uiDt * 2.6f, 0.f);
    if (panelT <= 0.f) { tabT = 0.f; lastTab = -1; selY = -1.f; tabPX = -1.f; return; }
    if (gPTab != lastTab) { lastTab = gPTab; tabT = 0.f; selY = -1.f; }
    else tabT += uiDt;
    const float p = smooth01(panelT / 0.30f);
    float eo = clampv(panelT / 0.42f, 0.f, 1.f); eo = 1.f - (1.f - eo) * (1.f - eo) * (1.f - eo);       // ease-out slide
    const float stag = std::min(panelT - 0.10f, tabT);                                                  // row stagger clock
    auto rowA = [&](int k) { return smooth01((stag - k * 0.03f) / 0.22f); };
    auto wh = [&](float t, float a) { D2D1_COLOR_F c = whiten(accent, t); c.a = a; return c; };
    auto addH = [&](int id, float a, float b, float c, float d) { if (gPanel) addHit(id, a, b, c, d); };

    float w = std::min(540 * ui, (float)W - 40 * ui), rowH = 32 * ui, padX = 22 * ui, topH = 104 * ui, footH = 40 * ui;
    size_t mt = 13; for (int t = 0; t < 5; t++) mt = std::max(mt, gTabRows[t].size());
    int maxRows = std::max(5, (int)(((float)H - 40 * ui - topH - footH) / rowH));
    int shown = std::min((int)mt, maxRows), n = panelRows();
    float h = topH + shown * rowH + footH;
    float x = 24 * ui - (1.f - eo) * 90 * ui, y = std::max(20 * ui, ((float)H - h) * 0.5f);

    br->SetColor(D2D1::ColorF(0, 0, 0, 0.16f * p)); rt->FillRectangle(D2D1::RectF(0, 0, (float)W, (float)H), br.Get());        // focus dim
    ctx->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), p), nullptr);
    drawPane(x, y, w, h, 20 * ui, accent, 1.25f);
    addH(H_PANEL, x, y, x + w, y + h);

    // ---- title + close ----
    text(L"Settings", x + padX, y + 14 * ui, 240 * ui, 32 * ui, fTitle.Get(), D2D1::ColorF(1, 1, 1, 0.95f));
    {
        float cx = x + w - padX - 12 * ui, cy = y + 30 * ui; bool hv = hov(cx - 14 * ui, cy - 14 * ui, cx + 14 * ui, cy + 14 * ui);
        br->SetColor(D2D1::ColorF(hv ? accent.r : 1.f, hv ? accent.g : 1.f, hv ? accent.b : 1.f, hv ? 0.35f : 0.08f)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 13 * ui, 13 * ui), br.Get());
        br->SetColor(D2D1::ColorF(1, 1, 1, hv ? 1.f : 0.7f));
        rt->DrawLine(D2D1::Point2F(cx - 4.5f * ui, cy - 4.5f * ui), D2D1::Point2F(cx + 4.5f * ui, cy + 4.5f * ui), br.Get(), 1.6f * ui, roundStroke.Get());
        rt->DrawLine(D2D1::Point2F(cx + 4.5f * ui, cy - 4.5f * ui), D2D1::Point2F(cx - 4.5f * ui, cy + 4.5f * ui), br.Get(), 1.6f * ui, roundStroke.Get());
        addH(H_PACT + 32, cx - 14 * ui, cy - 14 * ui, cx + 14 * ui, cy + 14 * ui);
    }

    // ---- tab strip:  [Q]  Look Color ...  [E] ----
    float ty = y + 56 * ui, th = 30 * ui, tgap = 4 * ui, capW = 24 * ui;
    float tws[NT], total = 0;
    for (int t = 0; t < NT; t++) { tws[t] = ((float)wcslen(kPTabNames[t]) * 6.8f + 22.f) * ui; total += tws[t] + (t ? tgap : 0.f); }
    float regionL = x + padX + capW + 6 * ui, regionR = x + w - padX - capW - 6 * ui;
    float tx = (regionL + regionR) * 0.5f - total * 0.5f;
    float actX = 0, actW = 0, tpos[NT];
    for (int t = 0; t < NT; t++) { tpos[t] = tx; if (t == gPTab) { actX = tx; actW = tws[t]; } tx += tws[t] + tgap; }
    if (tabPX < 0.f) { tabPX = actX - x; tabPW = actW; }
    else { float k = 1.f - expf(-uiDt * 18.f); tabPX += (actX - x - tabPX) * k; tabPW += (actW - tabPW) * k; }
    br->SetColor(D2D1::ColorF(accent.r, accent.g, accent.b, 0.28f)); fillRR(x + tabPX, ty, x + tabPX + tabPW, ty + th, th * 0.5f);
    br->SetColor(D2D1::ColorF(accent.r, accent.g, accent.b, 0.55f)); rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + tabPX, ty, x + tabPX + tabPW, ty + th), th * 0.5f, th * 0.5f), br.Get(), 1.f * ui);
    for (int t = 0; t < NT; t++) {
        bool act = (t == gPTab), hv = hov(tpos[t], ty, tpos[t] + tws[t], ty + th);
        if (hv && !act) { br->SetColor(D2D1::ColorF(1, 1, 1, 0.07f)); fillRR(tpos[t], ty, tpos[t] + tws[t], ty + th, th * 0.5f); }
        text(kPTabNames[t], tpos[t], ty, tws[t], th, fBtn.Get(), act ? wh(0.8f, 1.f) : D2D1::ColorF(1, 1, 1, hv ? 0.95f : 0.58f));
        addH(H_PTAB + t, tpos[t], ty, tpos[t] + tws[t], ty + th);
    }
    for (int s = 0; s < 2; s++) {                                           // Q / E keycaps (also clickable)
        float cx0 = s == 0 ? x + padX : x + w - padX - capW; bool hv = hov(cx0, ty + 3 * ui, cx0 + capW, ty + th - 3 * ui);
        br->SetColor(D2D1::ColorF(1, 1, 1, hv ? 0.18f : 0.08f)); fillRR(cx0, ty + 3 * ui, cx0 + capW, ty + th - 3 * ui, 6 * ui);
        br->SetColor(D2D1::ColorF(1, 1, 1, hv ? 0.36f : 0.22f)); rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx0, ty + 3 * ui, cx0 + capW, ty + th - 3 * ui), 6 * ui, 6 * ui), br.Get(), 1.f * ui);
        br->SetColor(D2D1::ColorF(0, 0, 0, 0.25f)); rt->DrawLine(D2D1::Point2F(cx0 + 5 * ui, ty + th - 3.5f * ui), D2D1::Point2F(cx0 + capW - 5 * ui, ty + th - 3.5f * ui), br.Get(), 1.f * ui);   // keycap depth
        text(s == 0 ? L"Q" : L"E", cx0, ty + 3 * ui, capW, th - 6 * ui, fBtn.Get(), D2D1::ColorF(1, 1, 1, hv ? 1.f : 0.7f));
        addH(H_PACT + 40 + s, cx0, ty + 3 * ui, cx0 + capW, ty + th - 3 * ui);
    }
    br->SetColor(D2D1::ColorF(1, 1, 1, 0.10f)); rt->FillRectangle(D2D1::RectF(x + padX, y + 94 * ui, x + w - padX, y + 94 * ui + 1.f), br.Get());

    // ---- content ----
    float ry0 = y + topH, first = 0;
    float cx0 = x + padX + 214 * ui, cx1 = x + w - padX;
    gPSX0 = cx0 + 28 * ui; gPSX1 = cx1 - 92 * ui;
    if (gPTab < 5) {
        const std::vector<int>& rows = gTabRows[gPTab];
        int fi = clampv(gPRow - shown / 2, 0, std::max(0, n - shown)); first = (float)fi;
        if (n > 0) {                                                        // gliding selection bar
            float target = (ry0 - y) + (gPRow - fi) * rowH;
            if (selY < 0.f) selY = target; else selY += (target - selY) * (1.f - expf(-uiDt * 22.f));
            float a = rowA(std::max(0, gPRow - fi));
            br->SetColor(D2D1::ColorF(accent.r, accent.g, accent.b, 0.16f * a)); fillRR(x + 10 * ui, y + selY, x + w - 10 * ui, y + selY + rowH, 9 * ui);
            br->SetColor(D2D1::ColorF(accent.r, accent.g, accent.b, 0.95f * a)); fillRR(x + 10 * ui, y + selY + 7 * ui, x + 13 * ui, y + selY + rowH - 7 * ui, 1.5f * ui);
        }
        for (int k = 0; k < shown && fi + k < n; k++) {
            int idx = fi + k; Param& pr = P[rows[idx]]; float ry = ry0 + k * rowH, cy = ry + rowH * 0.5f;
            bool sel = (idx == gPRow); float a = rowA(k), ox = (1.f - a) * 14 * ui;
            D2D1_COLOR_F vc = sel ? wh(0.7f, a) : D2D1::ColorF(1, 1, 1, 0.9f * a);
            addH(H_PROW + idx, x + 8 * ui, ry, x + w - 8 * ui, ry + rowH);
            text(pr.name, x + padX + ox, ry, 205 * ui, rowH, fL.Get(), D2D1::ColorF(1, 1, 1, (sel ? 1.f : 0.74f) * a));
            if (pr.labels) {                                                // option:   <  value  >
                float mid = (cx0 + cx1) * 0.5f; bool hl = hov(cx0, ry, mid, ry + rowH), hr = hov(mid, ry, cx1, ry + rowH);
                arrowTri(cx0 + 10 * ui + ox, cy, 5 * ui, -1, D2D1::ColorF(1, 1, 1, (hl ? 1.f : (sel ? 0.8f : 0.38f)) * a));
                arrowTri(cx1 - 10 * ui + ox, cy, 5 * ui, 1, D2D1::ColorF(1, 1, 1, (hr ? 1.f : (sel ? 0.8f : 0.38f)) * a));
                br->SetColor(D2D1::ColorF(1, 1, 1, (sel ? 0.07f : 0.035f) * a)); fillRR(cx0 + 22 * ui + ox, cy - 11 * ui, cx1 - 22 * ui + ox, cy + 11 * ui, 11 * ui);
                text(valStr(pr), cx0 + 22 * ui + ox, ry, cx1 - cx0 - 44 * ui, rowH, fBtn.Get(), vc);
                addH(H_PL + idx, cx0, ry, mid, ry + rowH); addH(H_PR + idx, mid, ry, cx1, ry + rowH);
            }
            else {                                                        // slider:  <  ====o----  >  value
                float tr0 = gPSX0 + ox, tr1 = gPSX1 + ox, r = clampv((*pr.v - pr.mn) / (pr.mx - pr.mn), 0.f, 1.f);
                bool hl = hov(cx0 - 2 * ui, ry, cx0 + 22 * ui, ry + rowH), hr = hov(tr1 + 2 * ui, ry, tr1 + 26 * ui, ry + rowH);
                arrowTri(cx0 + 10 * ui + ox, cy, 5 * ui, -1, D2D1::ColorF(1, 1, 1, (hl ? 1.f : (sel ? 0.8f : 0.38f)) * a));
                arrowTri(tr1 + 14 * ui, cy, 5 * ui, 1, D2D1::ColorF(1, 1, 1, (hr ? 1.f : (sel ? 0.8f : 0.38f)) * a));
                br->SetColor(D2D1::ColorF(1, 1, 1, 0.14f * a)); fillRR(tr0, cy - 2 * ui, tr1, cy + 2 * ui, 2 * ui);
                br->SetColor(D2D1::ColorF(accent.r, accent.g, accent.b, (sel ? 0.95f : 0.6f) * a)); fillRR(tr0, cy - 2 * ui, tr0 + (tr1 - tr0) * r, cy + 2 * ui, 2 * ui);
                float kx = tr0 + (tr1 - tr0) * r, kr = (sel ? 7.f : 5.5f) * ui;
                if (sel) { br->SetColor(D2D1::ColorF(accent.r, accent.g, accent.b, 0.25f * a)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, cy), kr + 5 * ui, kr + 5 * ui), br.Get()); }
                br->SetColor(D2D1::ColorF(0, 0, 0, 0.25f * a)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, cy + 1 * ui), kr + 1 * ui, kr + 1 * ui), br.Get());
                br->SetColor(D2D1::ColorF(1, 1, 1, 0.96f * a)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, cy), kr, kr), br.Get());
                text(valStr(pr), cx1 - 58 * ui + ox, ry, 58 * ui, rowH, fR.Get(), vc);
                addH(H_PL + idx, cx0 - 2 * ui, ry, cx0 + 22 * ui, ry + rowH); addH(H_PR + idx, tr1 + 2 * ui, ry, tr1 + 26 * ui, ry + rowH);
                addH(H_PSLD + idx, gPSX0 - 8 * ui, ry + 2 * ui, gPSX1 + 8 * ui, ry + rowH - 2 * ui);
            }
        }
    }
    else if (gPTab == 5) {
        drawPresetsTab(x + padX, ry0 + 6 * ui, w - 2 * padX, accent, smooth01(stag / 0.25f));
    }
    else {
        int fi = clampv(gPScroll, 0, std::max(0, NSC - shown)); gPScroll = fi; first = (float)fi;
        for (int k = 0; k < shown && fi + k < NSC; k++) {
            float ry = ry0 + k * rowH, a = rowA(k), ox = (1.f - a) * 14 * ui; const Shortcut& sc2 = kShortcuts[fi + k];
            text(sc2.key, x + padX + ox, ry, 190 * ui, rowH, fMed.Get(), wh(0.65f, a));
            text(sc2.what, x + padX + 200 * ui + ox, ry, w - 2 * padX - 200 * ui, rowH, fL.Get(), D2D1::ColorF(1, 1, 1, 0.78f * a));
        }
    }
    if (gPTab != 5 && n > shown) {                                          // slim scrollbar
        float th2 = shown * rowH, tl = std::max(18.f * ui, th2 * shown / n), tp = ry0 + (th2 - tl) * (n - shown > 0 ? first / (n - shown) : 0.f);
        br->SetColor(D2D1::ColorF(1, 1, 1, 0.08f)); fillRR(x + w - 11 * ui, ry0, x + w - 8 * ui, ry0 + th2, 1.5f * ui);
        br->SetColor(D2D1::ColorF(accent.r, accent.g, accent.b, 0.6f)); fillRR(x + w - 11 * ui, tp, x + w - 8 * ui, tp + tl, 1.5f * ui);
    }

    // ---- footer hints ----
    float fy = ry0 + shown * rowH;
    br->SetColor(D2D1::ColorF(1, 1, 1, 0.08f)); rt->FillRectangle(D2D1::RectF(x + padX, fy + 4 * ui, x + w - padX, fy + 4 * ui + 1.f), br.Get());
    const wchar_t* hint = gPTab < 5 ? L"Q/E tabs      \u2191\u2193 select      \u2190\u2192 change      Right-click reset      Tab close"
        : (gPTab == 5 ? L"Click a look to apply it      Ctrl+C copy code      Ctrl+V paste code" : L"Mouse wheel scroll      Q/E tabs      Tab close");
    text(hint, x + padX, fy + 6 * ui, w - 2 * padX, footH - 8 * ui, fSmall.Get(), D2D1::ColorF(1, 1, 1, 0.45f * smooth01(stag / 0.3f)));
    ctx->PopLayer();
}
// ---- plain transparent pane: a dark see-through fill with a faint theme tint; no blur, sheen or rim ----

void Renderer::drawPane(float x, float y, float w, float h, float r, D2D1_COLOR_F ac, float weight) {
    const float a = clampv(S.panelAlpha * weight, 0.f, 0.95f);                                  // "Panel opacity" setting
    br->SetColor(D2D1::ColorF(0.01f, 0.01f, 0.025f, a)); fillRR(x, y, x + w, y + h, r);
    br->SetColor(D2D1::ColorF(ac.r, ac.g, ac.b, 0.05f * std::min(1.f, a * 2.f))); fillRR(x, y, x + w, y + h, r);
}
// ---- Spotify now-playing card ----

void Renderer::iconPlay(float cx, float cy, float s) { fillPoly({ D2D1::Point2F(cx - s * 0.38f, cy - s * 0.55f), D2D1::Point2F(cx - s * 0.38f, cy + s * 0.55f), D2D1::Point2F(cx + s * 0.62f, cy) }); }
void Renderer::iconPause(float cx, float cy, float s) { fillRR(cx - s * 0.46f, cy - s * 0.5f, cx - s * 0.12f, cy + s * 0.5f, s * 0.12f); fillRR(cx + s * 0.12f, cy - s * 0.5f, cx + s * 0.46f, cy + s * 0.5f, s * 0.12f); }
void Renderer::iconNext(float cx, float cy, float s) { fillPoly({ D2D1::Point2F(cx - s * 0.5f, cy - s * 0.5f), D2D1::Point2F(cx - s * 0.5f, cy + s * 0.5f), D2D1::Point2F(cx + s * 0.35f, cy) }); fillRR(cx + s * 0.38f, cy - s * 0.5f, cx + s * 0.56f, cy + s * 0.5f, s * 0.09f); }
void Renderer::iconPrev(float cx, float cy, float s) { fillPoly({ D2D1::Point2F(cx + s * 0.5f, cy - s * 0.5f), D2D1::Point2F(cx + s * 0.5f, cy + s * 0.5f), D2D1::Point2F(cx - s * 0.35f, cy) }); fillRR(cx - s * 0.56f, cy - s * 0.5f, cx - s * 0.38f, cy + s * 0.5f, s * 0.09f); }
void Renderer::pill(const std::wstring& label, float x, float y, float w, float h, bool active, int id, D2D1_COLOR_F ac) {
    const bool hv = id && hovS(x, y, x + w, y + h);
    const float r = h * 0.5f;
    if (active) {
        br->SetColor(D2D1::ColorF(ac.r, ac.g, ac.b, hv ? 0.38f : 0.28f)); fillRR(x, y, x + w, y + h, r);
        br->SetColor(D2D1::ColorF(ac.r, ac.g, ac.b, 0.55f)); rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r), br.Get(), 1.f * ui);
    }
    else {
        br->SetColor(D2D1::ColorF(1, 1, 1, hv ? 0.12f : 0.055f)); fillRR(x, y, x + w, y + h, r);
        br->SetColor(D2D1::ColorF(1, 1, 1, hv ? 0.28f : 0.13f)); rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r), br.Get(), 1.f * ui);
    }
    D2D1_COLOR_F tc = active ? whiten(ac, 0.55f) : D2D1::ColorF(1, 1, 1, hv ? 0.95f : 0.72f); tc.a = active ? 1.f : tc.a;
    if (fBtn) { br->SetColor(tc); rt->DrawText(label.c_str(), (UINT32)label.size(), fBtn.Get(), D2D1::RectF(x, y, x + w, y + h), br.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP); }
    else text(label, x + 12 * ui, y, w - 24 * ui, h, fSmall.Get(), tc);
    if (id) addHit(id, x, y, x + w, y + h);
}
void Renderer::drawNowPlaying(double now, D2D1_COLOR_F ac) {
    PlayerState s = gSp.state(); int st = gSp.status;
    int npMode = (int)(S.nowPlaying + 0.5f);                        // 0 auto (shrinks), 1 always full, 2 always compact, 3 off
    bool over = gMouseX >= npBox[0] && gMouseX <= npBox[2] && gMouseY >= npBox[1] && gMouseY <= npBox[3];
    bool recent = (now - gLastInput < 6.0) || (now - gTrackT < 6.0) || gDrag != 0 || over;
    if (npMode >= 3) return;
    if (st != Spotify::ST_ON || !s.device || s.t.name.empty()) {
        if (!(recent || st == Spotify::ST_WAIT || st == Spotify::ST_ERR) || gOverlay != OV_NONE) return;
        std::wstring label; int id = H_CONNECT;
        switch (st) {
        case Spotify::ST_NOID: label = L"Connect Spotify  (S)"; break;
        case Spotify::ST_OFF:  label = gSp.message().empty() ? std::wstring(L"Connect Spotify  (S)") : toW(gSp.message()); break;
        case Spotify::ST_WAIT: label = L"Waiting for browser approval..."; id = 0; break;
        case Spotify::ST_ERR:  label = toW(gSp.message()) + L"   (S to retry)"; break;
        default:               label = L"Spotify connected - press play in Spotify"; id = 0; break;
        }
        float w = std::min((float)W - 40 * ui, ((float)label.size() * 8.4f + 40.f) * ui), x = (W - w) * 0.5f, y = H - 62 * ui;
        drawPane(x, y, w, 38 * ui, 19 * ui, ac, 1.0f);
        text(label, x + 18 * ui, y, w - 36 * ui, 38 * ui, fL.Get(), D2D1::ColorF(1, 1, 1, 0.9f));
        if (id) addHit(id, x, y, x + w, y + 38 * ui);
        return;
    }
    float npS = clampv(S.npSize, 0.5f, 1.3f);
    bool wantCompact = (npMode == 2) || (npMode == 0 && !over && gDrag == 0);   // Auto: compact until the mouse hovers over the card
    npC += ((wantCompact ? 1.f : 0.f) - npC) * (1.f - expf(-uiDt * 7.f));
    const float c = smooth01(npC), fa = 1.f - smooth01(c / 0.5f);          // c: 0 = full card, 1 = compact; fa: fades the full-only controls
    auto mix = [&](float a, float b) { return a + (b - a) * c; };
    float ta = (float)(now - gTrackT);                                      // seconds since the track changed
    float pop = ta >= 0.f && ta < 0.6f ? 0.93f + 0.07f * easeBack(ta / 0.55f) : 1.f;
    float k = npS * pop;                                                    // whole card is drawn scaled about its bottom-centre
    float wMax = ((float)W - 32 * ui) / npS;
    // the card sizes itself to the song: measure the title / artist and grow (or shrink) the card to fit, glided so track changes don't snap
    static std::string mId; static float mTitle = 0.f, mArtist = 0.f, wFullS = 0.f, wCompS = 0.f;
    if (mId != s.t.id + "|" + s.t.name) {
        mId = s.t.id + "|" + s.t.name;
        auto meas = [&](const std::wstring& str, IDWriteTextFormat* f) -> float {
            ComPtr<IDWriteTextLayout> tl; if (!f || FAILED(dw->CreateTextLayout(str.c_str(), (UINT32)str.size(), f, 4000.f, 100.f, &tl)) || !tl) return 0.f;
            DWRITE_TEXT_METRICS tm = {}; tl->GetMetrics(&tm); return tm.widthIncludingTrailingWhitespace;
            };
        mTitle = meas(toW(s.t.name), fTitle.Get()); mArtist = meas(toW(s.t.artist), fL.Get());
    }
    float wFullT = clampv(std::max(mTitle, mArtist) + 158.f * ui, 700.f * ui, std::max(700.f * ui, std::min(940.f * ui, wMax)));
    float wCompT = clampv(mTitle + 208.f * ui, 300.f * ui, std::max(300.f * ui, std::min(600.f * ui, wMax)));
    wFullT = std::min(wFullT, wMax); wCompT = std::min(wCompT, wMax);
    if (wFullS <= 0.f) { wFullS = wFullT; wCompS = wCompT; }
    float ka = 1.f - expf(-uiDt * 9.f); wFullS += (wFullT - wFullS) * ka; wCompS += (wCompT - wCompS) * ka;
    float w = mix(wFullS, wCompS), h = mix(134.f, 64.f) * ui;
    float x = (W - w) * 0.5f, y = H - h - 22 * ui;
    hitK = k; hitPx = W * 0.5f; hitPy = H - 22 * ui;
    npBox[0] = hitPx + (x - hitPx) * k - 8; npBox[1] = hitPy + (y - hitPy) * k - 8;      // screen box, used to keep the card open while hovered
    npBox[2] = hitPx + (x + w - hitPx) * k + 8; npBox[3] = hitPy + (y + h - hitPy) * k + 8;
    rt->SetTransform(D2D1::Matrix3x2F::Scale(k, k, D2D1::Point2F(hitPx, hitPy)));

    // ---- queue peek: "up next" rises out of the card while it is open ----
    {
        if (c > 0.6f) gQueueOpen = false;                                        // card shrank: the list closes with it
        bool openCard = c < 0.35f && fa > 0.5f && gQueueOpen;                    // only when you've asked for it with the queue button
        if (openCard) gSp.wantQueue();
        std::vector<TrackInfo> qv; std::string qFor; gSp.getQueue(qv, qFor);
        bool fresh = qFor == s.t.id;
        qT += ((openCard && fresh ? 1.f : 0.f) - qT) * (1.f - expf(-uiDt * 10.f));
        if (qT > 0.01f) {
            const float e = smooth01(qT), rowH = 40.f * ui, headH = 30.f * ui, gap = 10.f * ui;
            const int n = std::max(1, std::min(3, (int)qv.size()));
            const float qh = headH + n * rowH + 8.f * ui, qw = w, qx = x, qy = y - gap - qh + (1.f - e) * 18.f * ui;
            npBox[1] = std::min(npBox[1], hitPy + (qy - hitPy) * k - 8);               // hovering the list keeps the card open
            ctx->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), e), nullptr);
            drawPane(qx, qy, qw, qh, 18.f * ui, ac, 1.0f);
            addHit(H_PANEL, qx, qy, qx + qw, qy + qh);
            D2D1_COLOR_F lab = whiten(ac, 0.45f);
            text(L"UP NEXT", qx + 18.f * ui, qy + 4.f * ui, 200.f * ui, headH - 4.f * ui, fSmall.Get(), D2D1::ColorF(lab.r, lab.g, lab.b, 0.9f));
            if (qv.empty()) text(L"Nothing queued", qx + 18.f * ui, qy + headH, qw - 36.f * ui, rowH, fL.Get(), D2D1::ColorF(1, 1, 1, 0.5f));
            for (int i = 0; i < (int)qv.size() && i < 3; i++) {
                float ry = qy + headH + i * rowH, ri = smooth01(clampv((qT - 0.15f * i) / 0.7f, 0.f, 1.f));
                bool hv = hovS(qx + 8.f * ui, ry, qx + qw - 8.f * ui, ry + rowH);
                if (hv) { br->SetColor(D2D1::ColorF(1, 1, 1, 0.08f)); fillRR(qx + 8.f * ui, ry + 2.f * ui, qx + qw - 8.f * ui, ry + rowH - 2.f * ui, 10.f * ui); }
                float ox = (1.f - ri) * 12.f * ui;
                wchar_t num[4]; swprintf_s(num, L"%d", i + 1);
                br->SetColor(D2D1::ColorF(1, 1, 1, (hv ? 0.9f : 0.4f) * ri));
                rt->DrawText(num, (UINT32)wcslen(num), fBtn.Get(), D2D1::RectF(qx + 12.f * ui + ox, ry, qx + 36.f * ui + ox, ry + rowH), br.Get());
                if (hv) { br->SetColor(D2D1::ColorF(1, 1, 1, 0.95f)); iconPlay(qx + 24.f * ui + ox, ry + rowH * 0.5f, 9.f * ui); }   // hover: the number becomes a play icon
                float tx0 = qx + 46.f * ui + ox, durW = 54.f * ui, tw0 = qw - (tx0 - qx) - durW - 18.f * ui;
                std::wstring tn = toW(qv[i].name), ta2 = toW(qv[i].artist);
                text(tn, tx0, ry + 3.f * ui, tw0, rowH * 0.5f, fMed.Get(), D2D1::ColorF(1, 1, 1, 0.95f * ri));
                text(ta2, tx0, ry + rowH * 0.5f - 1.f * ui, tw0, rowH * 0.5f, fSmall.Get(), D2D1::ColorF(1, 1, 1, 0.55f * ri));
                std::wstring du = mmss(qv[i].durMs);
                br->SetColor(D2D1::ColorF(1, 1, 1, 0.45f * ri));
                rt->DrawText(du.c_str(), (UINT32)du.size(), fR.Get(), D2D1::RectF(qx + qw - durW - 16.f * ui, ry, qx + qw - 16.f * ui, ry + rowH), br.Get());
                if (qT > 0.6f) addHit(H_Q0 + i, qx + 8.f * ui, ry, qx + qw - 8.f * ui, ry + rowH);
            }
            ctx->PopLayer();
        }
    }

    drawPane(x, y, w, h, mix(24.f, 20.f) * ui, ac, 1.0f); addHit(H_PANEL, x, y, x + w, y + h);

    // cover
    float as = mix(104.f, 48.f) * ui, ax = x + mix(14.f, 8.f) * ui, ay = y + mix(14.f, 8.f) * ui, arad = mix(10.f, 8.f) * ui;
    if (art && artBrush) {
        float ap = ta >= 0.f && ta < 0.8f ? 0.62f + 0.38f * easeBack(ta / 0.7f) : 1.f;   // cover pops in on a new track
        float as2 = as * ap, ax2 = ax + (as - as2) * 0.5f, ay2 = ay + (as - as2) * 0.5f;
        if (artPrev && artBrushP) {
            float sp = as2 / artPrevW;
            artBrushP->SetTransform(D2D1::Matrix3x2F::Scale(sp, sp) * D2D1::Matrix3x2F::Translation(ax2, ay2));
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(ax2, ay2, ax2 + as2, ay2 + as2), arad, arad), artBrushP.Get());
        }
        float scl = as2 / artW;
        artBrush->SetTransform(D2D1::Matrix3x2F::Scale(scl, scl) * D2D1::Matrix3x2F::Translation(ax2, ay2));
        artBrush->SetOpacity(artPrev ? smooth01(artFade) : 1.f);
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(ax2, ay2, ax2 + as2, ay2 + as2), arad, arad), artBrush.Get());
        artBrush->SetOpacity(1.f);
        br->SetColor(D2D1::ColorF(1, 1, 1, 0.12f)); rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(ax2, ay2, ax2 + as2, ay2 + as2), arad, arad), br.Get(), 1.f * ui);
    }
    else {
        br->SetColor(D2D1::ColorF(1, 1, 1, 0.07f)); fillRR(ax, ay, ax + as, ay + as, arad);
        text(L"\u266A", ax, ay, as, as, fTitle.Get(), D2D1::ColorF(1, 1, 1, 0.35f));
    }

    // title (the artist line belongs to the full card)
    float txF = x + 130 * ui, tx = mix(txF, x + 66 * ui);
    float prevCx = x + w - 104 * ui;                                         // compact: centre of the prev button
    float twF = w - 146 * ui - 40 * ui, twC = prevCx - 22 * ui - (x + 66 * ui);
    float a1 = smooth01((ta - 0.10f) / 0.45f), a2 = smooth01((ta - 0.20f) / 0.45f);   // title, then artist, slide up + fade in
    text(toW(s.t.name), tx, mix(y + 8 * ui, y + (h - 28 * ui) * 0.5f) + (1.f - a1) * 14 * ui, mix(twF, std::max(40.f * ui, twC)), 28 * ui, fTitle.Get(), D2D1::ColorF(1, 1, 1, 0.98f * a1));

    // prev / play / next glide between the two layouts
    float cyF = y + 80 * ui, cy = mix(cyF, y + h * 0.5f);
    float pxF = txF + 66 * ui + 44 * ui, pcxF = pxF + 46 * ui, nxF = pcxF + 46 * ui;
    float nxC = x + w - 24 * ui, pcxC = nxC - 40 * ui, pxC = pcxC - 40 * ui;
    float px = mix(pxF, pxC), pcx = mix(pcxF, pcxC), nx = mix(nxF, nxC), isz = mix(14.f, 11.f) * ui, pr = mix(18.f, 15.f) * ui;
    {   // transport: soft hover discs behind prev / next, the play button lifts a little on hover
        const float hb = 17 * ui;
        float hP = hoverAnim(0, hovS(px - hb, cy - hb, px + hb, cy + hb)), hN = hoverAnim(1, hovS(nx - hb, cy - hb, nx + hb, cy + hb));
        float hPl = hoverAnim(2, hovS(pcx - pr - 3 * ui, cy - pr - 3 * ui, pcx + pr + 3 * ui, cy + pr + 3 * ui));
        if (hP > 0.01f) { br->SetColor(D2D1::ColorF(1, 1, 1, 0.12f * hP)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px, cy), hb, hb), br.Get()); }
        if (hN > 0.01f) { br->SetColor(D2D1::ColorF(1, 1, 1, 0.12f * hN)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(nx, cy), hb, hb), br.Get()); }
        br->SetColor(D2D1::ColorF(1, 1, 1, 0.78f + 0.2f * hP)); iconPrev(px, cy, isz); addHit(H_PREV, px - hb, cy - hb, px + hb, cy + hb);
        br->SetColor(D2D1::ColorF(1, 1, 1, 0.78f + 0.2f * hN)); iconNext(nx, cy, isz); addHit(H_NEXT, nx - hb, cy - hb, nx + hb, cy + hb);
        float pr2 = pr * (1.f + 0.07f * hPl);
        br->SetColor(D2D1::ColorF(0, 0, 0, 0.22f)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(pcx, cy + 2.f * ui), pr2 + 1.f * ui, pr2 + 1.f * ui), br.Get());   // contact shadow
        if (hPl > 0.01f) { br->SetColor(D2D1::ColorF(ac.r, ac.g, ac.b, 0.28f * hPl)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(pcx, cy), pr2 + 5 * ui, pr2 + 5 * ui), br.Get()); }
        br->SetColor(D2D1::ColorF(1, 1, 1, 0.97f)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(pcx, cy), pr2, pr2), br.Get());
        br->SetColor(D2D1::ColorF(0.05f, 0.05f, 0.08f, 1.f)); if (s.playing) iconPause(pcx, cy, isz); else iconPlay(pcx + isz * 0.06f, cy, isz);
        addHit(H_PLAY, pcx - pr - 3 * ui, cy - pr - 3 * ui, pcx + pr + 3 * ui, cy + pr + 3 * ui);
    }

    if (fa > 0.01f) {                                                        // full-card extras fade out as the card shrinks
        ctx->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), fa), nullptr);
        const bool act = c < 0.35f;                                          // only clickable while (nearly) full
        {   // "Up next" toggle: list icon, top-right of the card
            float bs = 30.f * ui, bx = x + w - 16.f * ui - bs, byy = y + 12.f * ui;
            bool hv = act && hovS(bx, byy, bx + bs, byy + bs);
            if (gQueueOpen) { br->SetColor(D2D1::ColorF(ac.r, ac.g, ac.b, hv ? 0.42f : 0.30f)); fillRR(bx, byy, bx + bs, byy + bs, 9.f * ui); }
            else if (hv) { br->SetColor(D2D1::ColorF(1, 1, 1, 0.12f)); fillRR(bx, byy, bx + bs, byy + bs, 9.f * ui); }
            D2D1_COLOR_F ic = gQueueOpen ? whiten(ac, 0.6f) : D2D1::ColorF(1, 1, 1, hv ? 0.95f : 0.65f); ic.a = gQueueOpen ? 1.f : ic.a;
            br->SetColor(ic);
            float lx0 = bx + 8.f * ui, lx1 = bx + bs - 8.f * ui, cyb = byy + bs * 0.5f;
            for (int i = -1; i <= 1; i++) {
                float ly = cyb + i * 5.5f * ui;
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(lx0 + 1.2f * ui, ly), 1.6f * ui, 1.6f * ui), br.Get());
                rt->DrawLine(D2D1::Point2F(lx0 + 5.5f * ui, ly), D2D1::Point2F(lx1, ly), br.Get(), 1.8f * ui, roundStroke.Get());
            }
            if (act) addHit(H_QBTN, bx, byy, bx + bs, byy + bs);
        }
        text(toW(s.t.artist), txF, y + 34 * ui + (1.f - a2) * 14 * ui, twF, 22 * ui, fL.Get(), D2D1::ColorF(1, 1, 1, 0.65f * a2));
        pill(L"Shuffle", txF, cyF - 13 * ui, 66 * ui, 26 * ui, s.shuffle, act ? H_SHUF : 0, ac);
        std::wstring rl = s.repeat == "track" ? std::wstring(L"Repeat 1") : (s.repeat == "context" ? std::wstring(L"Repeat all") : std::wstring(L"Repeat"));
        pill(rl, nxF + 34 * ui, cyF - 13 * ui, 82 * ui, 26 * ui, s.repeat != "off", act ? H_REP : 0, ac);

        // volume
        float vw = 110 * ui, vx1 = x + w - 52 * ui, vx0 = vx1 - vw;
        if (nxF + 34 * ui + 82 * ui + 16 * ui < vx0 - 30 * ui) {
            int vol = (now < gVolUntil || gDrag == H_VOL) ? gVol : s.volume;
            float hv = hoverAnim(3, act && (gDrag == H_VOL || hovS(vx0 - 8 * ui, cyF - 14 * ui, vx1 + 8 * ui, cyF + 14 * ui)));
            float th = (2.f + 1.f * hv) * ui, vk = vx0 + vw * vol / 100.f;
            {   // little speaker glyph instead of the word "Vol"
                float sx = vx0 - 26 * ui; br->SetColor(D2D1::ColorF(1, 1, 1, 0.55f + 0.35f * hv));
                fillRR(sx, cyF - 3 * ui, sx + 4 * ui, cyF + 3 * ui, 1 * ui);
                fillPoly({ D2D1::Point2F(sx + 3 * ui, cyF - 3 * ui), D2D1::Point2F(sx + 9 * ui, cyF - 7 * ui), D2D1::Point2F(sx + 9 * ui, cyF + 7 * ui), D2D1::Point2F(sx + 3 * ui, cyF + 3 * ui) });
                if (vol > 0) rt->DrawLine(D2D1::Point2F(sx + 12 * ui, cyF - 3 * ui), D2D1::Point2F(sx + 12 * ui, cyF + 3 * ui), br.Get(), 1.4f * ui, roundStroke.Get());
                if (vol > 50) rt->DrawLine(D2D1::Point2F(sx + 15.5f * ui, cyF - 5.5f * ui), D2D1::Point2F(sx + 15.5f * ui, cyF + 5.5f * ui), br.Get(), 1.4f * ui, roundStroke.Get());
            }
            br->SetColor(D2D1::ColorF(1, 1, 1, 0.14f + 0.06f * hv)); fillRR(vx0, cyF - th, vx1, cyF + th, th);
            br->SetColor(D2D1::ColorF(ac.r, ac.g, ac.b, 0.95f)); fillRR(vx0, cyF - th, std::max(vx0 + 2 * th, vk), cyF + th, th);
            float kr = (4.5f + 2.f * hv) * ui;
            br->SetColor(D2D1::ColorF(0, 0, 0, 0.25f)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(vk, cyF + 1 * ui), kr + 1 * ui, kr + 1 * ui), br.Get());
            br->SetColor(D2D1::ColorF(1, 1, 1, 0.97f)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(vk, cyF), kr, kr), br.Get());
            wchar_t vb[8]; swprintf_s(vb, L"%d", vol);
            text(vb, vx1 + 8 * ui, cyF - 12 * ui, 36 * ui, 24 * ui, fSmall.Get(), D2D1::ColorF(1, 1, 1, 0.6f));
            if (act) { gVolX0 = hitPx + (vx0 - hitPx) * hitK; gVolX1 = hitPx + (vx1 - hitPx) * hitK; addHit(H_VOL, vx0 - 8 * ui, cyF - 14 * ui, vx1 + 8 * ui, cyF + 14 * ui); }
        }

        // progress
        float bx0 = txF, bx1 = x + w - 16 * ui, by = y + 108 * ui;
        double prog = s.progMs + (s.playing ? (now - s.stamp) * 1000.0 : 0.0); if (prog > s.t.durMs) prog = s.t.durMs;
        float frac = s.t.durMs > 0 ? (float)(prog / s.t.durMs) : 0.f;
        if (gDrag == H_PROG || now < gSeekUntil) frac = gSeekFrac;
        float hv = hoverAnim(4, act && (gDrag == H_PROG || hovS(bx0 - 6 * ui, by - 12 * ui, bx1 + 6 * ui, by + 12 * ui)));
        float th = (2.f + 1.f * hv) * ui, pk = bx0 + (bx1 - bx0) * frac, kr = (3.5f + 3.f * hv) * ui;
        br->SetColor(D2D1::ColorF(1, 1, 1, 0.14f + 0.06f * hv)); fillRR(bx0, by - th, bx1, by + th, th);
        br->SetColor(D2D1::ColorF(ac.r, ac.g, ac.b, 0.95f)); fillRR(bx0, by - th, std::max(bx0 + 2 * th, pk), by + th, th);
        br->SetColor(D2D1::ColorF(0, 0, 0, 0.25f * hv)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(pk, by + 1 * ui), kr + 1 * ui, kr + 1 * ui), br.Get());
        br->SetColor(D2D1::ColorF(1, 1, 1, 0.97f)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(pk, by), kr, kr), br.Get());
        text(mmss((int)(frac * s.t.durMs)), bx0, by + 5 * ui, 60 * ui, 16 * ui, fSmall.Get(), D2D1::ColorF(1, 1, 1, 0.55f));
        std::wstring dur = mmss(s.t.durMs);
        br->SetColor(D2D1::ColorF(1, 1, 1, 0.55f)); rt->DrawText(dur.c_str(), (UINT32)dur.size(), fSmall.Get(), D2D1::RectF(bx1 - 60 * ui, by + 5 * ui, bx1, by + 21 * ui), br.Get());
        if (act) { gProgX0 = hitPx + (bx0 - hitPx) * hitK; gProgX1 = hitPx + (bx1 - hitPx) * hitK; addHit(H_PROG, bx0 - 6 * ui, by - 12 * ui, bx1 + 6 * ui, by + 12 * ui); }
        ctx->PopLayer();
    }
    rt->SetTransform(D2D1::Matrix3x2F::Identity()); hitK = 1.f;
}
// ---- first-time Spotify setup overlay ----

void Renderer::drawSetup(double now, D2D1_COLOR_F ac) {
    br->SetColor(D2D1::ColorF(0, 0, 0, 0.55f)); rt->FillRectangle(D2D1::RectF(0, 0, (float)W, (float)H), br.Get());
    float pw = std::min(720.f * ui, (float)W - 40 * ui), ph = 360 * ui, x = (W - pw) * 0.5f, y = std::max(24.f * ui, (H - ph) * 0.4f);
    drawPane(x, y, pw, ph, 16 * ui, ac, 1.1f); addHit(H_PANEL, x, y, x + pw, y + ph);
    float lx = x + 26 * ui, lw = pw - 52 * ui, ly = y + 12 * ui; D2D1_COLOR_F wh = D2D1::ColorF(1, 1, 1, 0.85f);
    text(L"Connect Spotify", lx, ly, lw, 40 * ui, fTitle.Get(), D2D1::ColorF(1, 1, 1, 1));
    text(L"One-time setup. Spotify requires Premium, and the app owner needs Premium too.", lx, ly + 40 * ui, lw, 24 * ui, fL.Get(), D2D1::ColorF(1, 1, 1, 0.6f));
    text(L"1.  Open developer.spotify.com/dashboard and create an app (tick \"Web API\").", lx, ly + 76 * ui, lw, 24 * ui, fL.Get(), wh);
    text(L"2.  Add this Redirect URI exactly:  http://127.0.0.1:8888/callback", lx, ly + 102 * ui, lw - 130 * ui, 24 * ui, fL.Get(), wh);
    pill(L"Copy URI", x + pw - 26 * ui - 100 * ui, ly + 102 * ui, 100 * ui, 24 * ui, false, H_COPY, ac);
    text(L"3.  Paste the app's Client ID below (Ctrl+V) and press Enter.", lx, ly + 128 * ui, lw, 24 * ui, fL.Get(), wh);
    float fy = ly + 168 * ui;
    fieldInset(lx, fy, lw, 44 * ui, 12 * ui, ac, 1.f);
    bool caret = fmod(now, 1.0) < 0.6;
    std::wstring shown = gInput.empty() ? std::wstring(L"Client ID") : gInput + (caret ? L"|" : L" ");
    text(shown, lx + 14 * ui, fy, lw - 28 * ui, 44 * ui, fMed.Get(), D2D1::ColorF(1, 1, 1, gInput.empty() ? 0.35f : 1.f));
    text(L"A browser tab opens so you can approve access. Your login never touches this app.", lx, fy + 54 * ui, lw, 22 * ui, fSmall.Get(), D2D1::ColorF(1, 1, 1, 0.5f));
    text(L"Enter  connect        Esc  cancel", lx, fy + 80 * ui, lw, 22 * ui, fSmall.Get(), D2D1::ColorF(1, 1, 1, 0.45f));
}