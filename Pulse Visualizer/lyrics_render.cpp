// Renderer: lyric layout, scrolling and drawing
#include "common.h"
#include "util.h"
#include "settings.h"
#include "color.h"
#include "analyzer.h"
#include "spotify.h"
#include "renderer.h"
#include "ui_state.h"

    // ---- lyrics overlay: continuous scrolling column (spring-smoothed position, per-line scale + fade) ----

bool Renderer::hasFont(const wchar_t* name) {
    ComPtr<IDWriteFontCollection> fc; UINT32 idx = 0; BOOL ex = FALSE;
    if (SUCCEEDED(dw->GetSystemFontCollection(&fc)) && fc) fc->FindFamilyName(name, &idx, &ex);
    return ex != FALSE;
}
void Renderer::makeLyricFont() {
    int sel = clampv((int)(S.lyricFont + 0.5f), 0, 3);
    const wchar_t* fam = L"Segoe UI"; DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_BOLD;
    DWRITE_FONT_STRETCH st = DWRITE_FONT_STRETCH_NORMAL; DWRITE_FONT_STYLE sty = DWRITE_FONT_STYLE_NORMAL;
    if (sel == 0) { if (hasFont(L"Segoe UI Variable Display")) fam = L"Segoe UI Variable Display"; }
    else if (sel == 1) { if (hasFont(L"Bahnschrift")) fam = L"Bahnschrift"; w = DWRITE_FONT_WEIGHT_SEMI_BOLD; }
    else if (sel == 2) { if (hasFont(L"Bahnschrift")) { fam = L"Bahnschrift"; st = DWRITE_FONT_STRETCH_CONDENSED; } w = DWRITE_FONT_WEIGHT_SEMI_BOLD; }
    else { if (hasFont(L"Georgia")) fam = L"Georgia"; sty = DWRITE_FONT_STYLE_ITALIC; }
    fLy.Reset();
    dw->CreateTextFormat(fam, nullptr, w, sty, st, 34.f, L"en-us", &fLy);
    if (fLy) { fLy->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); fLy->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR); fLy->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP); }
    lyFontSel = sel;
}
float Renderer::interp(const float* a, int n, float x) {
    x = clampv(x, 0.f, (float)(n - 1)); int i = (int)x; if (i >= n - 1) return a[n - 1];
    return a[i] + (a[i + 1] - a[i]) * (x - i);
}
void Renderer::buildLyricRows() {                                  // collapse runs of blank timestamps into a single music-note row
    lyD.clear();
    for (const LyricLine& l : lyr.lines) { if (l.text.empty() && !lyD.empty() && lyD.back().text.empty()) continue; lyD.push_back(l); }
}

void Renderer::prepareLyrics(float dt, double now) {
    for (int i = 0; i < 10; i++) lyItems[i].lay.Reset();
    lyN = 0; lyMsg.clear();
    if (S.lyrics < 0.5f) return;
    if (!fLy || lyFontSel != clampv((int)(S.lyricFont + 0.5f), 0, 3)) makeLyricFont();
    if (!fLy) return;
    bool side = lySide;
    PlayerState s = gSp.state();
    bool barsMode = (int)(S.mode + 0.5f) == 0;                              // Bars: lyrics go to the middle top automatically
    float anchorT = H * (side ? 0.5f : (barsMode ? 0.22f : (S.lyricPos < 0.5f ? 0.64f : (S.lyricPos < 1.5f ? 0.5f : 0.22f))));
    if (lyAnchorS < 0.f) lyAnchorS = anchorT; else lyAnchorS += (anchorT - lyAnchorS) * (1.f - expf(-dt * 6.f));      // glide when the visual changes
    lyAnchor = lyAnchorS;
    if (lyr.id.empty() || lyr.id != s.t.id) return;
    if (lyr.state == 2 || lyr.state == 3) {
        float a = clampv(1.f - ((float)(now - lyrAt) - 4.f) / 1.f, 0.f, 1.f) * smooth01(clampv((float)(now - lyrAt) / 0.6f, 0.f, 1.f));
        if (a > 0.f) { lyMsg = lyr.state == 3 ? L"Instrumental" : L"No lyrics found for this track"; lyMsgA = a; }
        return;
    }
    if (lyr.state != 1 || lyD.empty()) return;
    int total = (int)lyD.size();
    // Spotify's progress_ms is late, jittery and sometimes stale, so we keep our own free-running clock and steer it with a MEDIAN of the
    // last few poll errors (one-off stale / early readings are rejected, a steady drift in either direction is corrected smoothly).
    // Real seeks (more than 1.5 s off, forward at once or backward on two polls in a row) snap, and lyrics never step back a line by themselves.
    double raw = s.progMs + (s.playing ? (now - s.stamp) * 1000.0 : 0.0);
    static double clk = 0.0, lastStamp = 0.0, lastOff = 0.0, errs[5] = {}; static std::string clkId; static bool clkPlaying = false, snapped = true; static int badPolls = 0, lastCur = -1, ne = 0, ni = 0;
    if (clkId != s.t.id || !s.playing || !clkPlaying) { clk = raw; clkId = s.t.id; badPolls = 0; ne = ni = 0; snapped = true; lastStamp = s.stamp; }
    else {
        clk += dt * 1000.0;
        if (s.stamp != lastStamp) {                                            // a fresh poll arrived
            lastStamp = s.stamp; double e = raw - clk;
            if (e > 1500.0) { clk = raw; badPolls = 0; ne = ni = 0; snapped = true; }        // seek forward
            else if (e < -1500.0) { if (++badPolls >= 2) { clk = raw; badPolls = 0; ne = ni = 0; snapped = true; } }   // seek back (a single stale poll is ignored)
            else { badPolls = 0; errs[ni] = e; ni = (ni + 1) % 5; if (ne < 5) ne++; }
        }
        if (ne >= 2) {
            double t[5]; for (int i = 0; i < ne; i++) t[i] = errs[i];
            std::sort(t, t + ne); double med = t[ne / 2];
            double corr = med * (1.0 - exp(-(double)dt * 1.5));                 // steer toward the median, both directions
            clk += corr; for (int i = 0; i < 5; i++) errs[i] -= corr;
        }
    }
    clkPlaying = s.playing;
    if (S.lyricOffset != lastOff) { lastOff = S.lyricOffset; snapped = true; }
    double pos = clk + S.lyricOffset + 150.0;   // +150 ms lead: the glide finishes on the beat
    int cur = -1, lo = 0, hi = total - 1;
    while (lo <= hi) { int mid = (lo + hi) / 2; if (lyD[mid].ms <= pos) { cur = mid; lo = mid + 1; } else hi = mid - 1; }
    if (snapped) { lastCur = cur; snapped = false; }
    else if (cur < lastCur) cur = lastCur; else lastCur = cur;   // never step back a line by itself

    // critically damped spring toward the current line: eases in AND out, never overshoots
    float target = (float)cur;
    if (lyInit) { lyPos = target; lyVel = 0.f; lyInit = false; }                                    // new song: snap
    else {
        if (fabsf(target - lyPos) > 3.5f) { lyPos = target + (lyPos > target ? 3.5f : -3.5f); lyVel = 0.f; }   // seek: jump to just outside the faint lines, then glide the rest in

        float st = 0.32f, om = 2.f / st, x = om * dt, ex = 1.f / (1.f + x + 0.48f * x * x + 0.235f * x * x * x);
        float ch = lyPos - target, tmp = (lyVel + om * ch) * dt;
        lyVel = (lyVel - om * tmp) * ex; lyPos = target + (ch + tmp) * ex;
    }

    float fs = 34.f * std::max(0.55f, sc) * S.lyricSize * (side ? 1.12f : 1.f), gap = fs * 0.35f;
    lyMaxW = side ? (float)W * 0.31f : std::min((float)W * 0.84f, 1200.f * std::max(1.f, sc));
    float restX0 = (W - lyMaxW) * 0.5f, sideX0 = (float)W * 0.645f;
    lyX0 = side ? restX0 + (sideX0 - restX0) * sideT : restX0;
    auto mk = [&](const std::string& u8, float size) -> ComPtr<IDWriteTextLayout> {
        std::wstring w = u8.empty() ? std::wstring(L"\u266A") : toW(u8);
        ComPtr<IDWriteTextLayout> l;
        dw->CreateTextLayout(w.c_str(), (UINT32)w.size(), fLy.Get(), lyMaxW, 10000.f, &l);
        if (l) {
            DWRITE_TEXT_RANGE r = { 0, (UINT32)w.size() }; l->SetFontSize(size, r);
            l->SetTextAlignment(side ? DWRITE_TEXT_ALIGNMENT_LEADING : DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        return l;
        };
    int prevN = side ? 2 : 1, nextN = side ? 4 : 2;
    int fl = (int)floorf(lyPos); float frac = lyPos - (float)fl;
    int jlo = std::max(-1, fl - prevN), jhi = std::min(total - 1, fl + 1 + nextN);
    static const float sP[7] = { 1.f, .78f, .68f, .62f, .58f, .55f, .55f }, aP[7] = { 1.f, .60f, .42f, .28f, .16f, .08f, 0.f };
    static const float sN[4] = { 1.f, .62f, .55f, .50f }, aN[4] = { 1.f, .32f, .18f, .08f };
    for (int j = jlo; j <= jhi && lyN < 10; j++) {
        float d = (float)j - lyPos, ad = fabsf(d);
        LyItem& it = lyItems[lyN];
        if (d >= 0.f) { it.s = interp(sP, 7, ad); it.alpha = interp(aP, 7, ad) * clampv((float)(nextN + 1) - ad, 0.f, 1.f); }
        else { it.s = interp(sN, 4, ad); it.alpha = interp(aN, 4, ad) * clampv((float)(prevN + 1) - ad, 0.f, 1.f); }
        if (j < 0) it.alpha *= 0.4f;                                    // the music note shown before the first line
        if (it.alpha < 0.01f) continue;
        it.lay = mk(j < 0 ? std::string() : lyD[j].text, fs); if (!it.lay) continue;
        DWRITE_TEXT_METRICS tm = {}; it.lay->GetMetrics(&tm);
        it.w = tm.width; it.h = tm.height; it.left = tm.left; it.j = j;
        float w0 = clampv(1.f - ad, 0.f, 1.f); it.w0 = w0 * w0 * (3.f - 2.f * w0);
        lyN++;
    }
    if (lyN == 0) return;
    // stack the lines (spacing follows each line's current scale), then pin the "reading point" to the anchor
    for (int i = 0; i < lyN; i++) {
        LyItem& it = lyItems[i]; float hh = it.h * it.s;
        if (i == 0) it.yc = hh * 0.5f;
        else { const LyItem& pv = lyItems[i - 1]; it.yc = pv.yc + pv.h * pv.s * 0.5f + gap * 0.5f * (pv.s + it.s) + hh * 0.5f; }
    }
    int ia = -1, ib = -1;
    for (int i = 0; i < lyN; i++) { if (lyItems[i].j == fl) ia = i; if (lyItems[i].j == fl + 1) ib = i; }
    float C, hb;
    if (ia >= 0 && ib >= 0) { C = lyItems[ia].yc + frac * (lyItems[ib].yc - lyItems[ia].yc); hb = lyItems[ia].h * lyItems[ia].s * (1.f - frac) + lyItems[ib].h * lyItems[ib].s * frac; }
    else if (ia >= 0) { C = lyItems[ia].yc; hb = lyItems[ia].h * lyItems[ia].s; }
    else if (ib >= 0) { C = lyItems[ib].yc; hb = lyItems[ib].h * lyItems[ib].s; }
    else { lyN = 0; return; }
    lyBarH = hb;
    for (int i = 0; i < lyN; i++) lyItems[i].yc = lyAnchor + lyItems[i].yc - C;
}
void Renderer::drawLyrics() {
    if (S.lyrics < 0.5f) return;
    if (!lyMsg.empty()) {                                                   // "Instrumental" / "No lyrics": a pill floating just above the now-playing card
        D2D1_COLOR_F mc = palette(theme, 0.5f, phase);
        bool cardOn = gShowUI && S.nowPlaying < 2.5f && npBox[1] > 0.f;   // UI hidden (H): sit at the bottom instead of above an invisible card
        float cardTop = cardOn ? npBox[1] : H - 22 * ui;
        float pw = std::min((float)W - 40 * ui, ((float)lyMsg.size() * 10.5f + 96.f) * ui), ph = 40 * ui;
        float px0 = (W - pw) * 0.5f, py0 = cardTop - 14 * ui - ph + (1.f - lyMsgA) * 10 * ui;
        ctx->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), lyMsgA), nullptr);
        drawPane(px0, py0, pw, ph, ph * 0.5f, mc, 0.85f);
        float ex = px0 + 20 * ui, ec = py0 + ph * 0.5f;                     // tiny animated equaliser
        for (int i = 0; i < 3; i++) {
            float hh = (5.f + 7.f * (0.5f + 0.5f * sinf(phase * 5.f + i * 1.7f))) * ui;
            br->SetColor(D2D1::ColorF(mc.r, mc.g, mc.b, 0.9f));
            fillRR(ex + i * 6 * ui, ec - hh * 0.5f, ex + i * 6 * ui + 3.5f * ui, ec + hh * 0.5f, 1.7f * ui);
        }
        text(lyMsg, px0 + 44 * ui, py0, pw - 60 * ui, ph, fL.Get(), D2D1::ColorF(1, 1, 1, 0.9f));
        ctx->PopLayer();
    }
    if (lyN == 0) return;
    float x0 = lyX0, px = lySide ? x0 : x0 + lyMaxW * 0.5f;
    D2D1_COLOR_F ac = palette(theme, 0.5f, phase);
    int cm = clampv((int)(S.lyricColor + 0.5f), 0, 2);
    D2D1_COLOR_F cA = whiten(palette(theme, 0.08f, phase), 0.28f), cB = whiten(palette(theme, 0.92f, phase), 0.28f);
    if (cm == 1) { cA = D2D1::ColorF(1, 1, 1, 1); cB = cA; }
    else if (cm == 2) { cA = whiten(ac, 0.35f); cB = cA; }
    D2D1_COLOR_F tint = (cm == 1) ? D2D1::ColorF(1, 1, 1, 1) : whiten(ac, 0.82f);
    D2D1_COLOR_F shade = D2D1::ColorF(ac.r * 0.10f, ac.g * 0.10f, ac.b * 0.10f, 1.f);
    static const float so[3][2] = { { 0.f, 3.f },{ 2.f, 2.f },{ -2.f, 2.f } };
    const bool glowOn = S.lyricGlow > 0.01f && lyBlurS && lyBlurL && rec;
    for (int i = 0; i < lyN; i++) {
        LyItem& it = lyItems[i]; if (!it.lay) continue;
        D2D1_POINT_2F o = D2D1::Point2F(x0, it.yc - it.h * 0.5f);
        D2D1_MATRIX_3X2_F xf = D2D1::Matrix3x2F::Scale(it.s, it.s, D2D1::Point2F(px, it.yc));   // size changes glide with the scroll
        rt->SetTransform(xf);
        const float gw = glowOn ? it.w0 : 0.f;                                                  // how much this line glows (the sung one)
        float shA = 0.45f * it.alpha * (1.f - std::min(1.f, gw * 1.5f));                         // the glow replaces the dark shadow on the sung line
        if (shA > 0.005f) {
            br->SetColor(D2D1::ColorF(shade.r, shade.g, shade.b, shA));
            for (int k = 0; k < 3; k++) rt->DrawTextLayout(D2D1::Point2F(o.x + so[k][0] * sc, o.y + so[k][1] * sc), it.lay.Get(), br.Get());
        }
        if (it.w0 < 0.99f) { setc(tint, 1.f, it.alpha * (1.f - it.w0)); rt->DrawTextLayout(o, it.lay.Get(), br.Get()); }
        if (it.w0 > 0.01f) {                                                                    // current line: gradient, cross-faded in/out
            float gx1 = x0 + it.left, gx2 = gx1 + std::max(1.f, it.w);
            ComPtr<ID2D1LinearGradientBrush> gb = gradBrush(cA, cB, gx1, o.y, gx2, o.y, it.alpha * it.w0);
            rt->DrawTextLayout(o, it.lay.Get(), gb.Get());
            if (gw > 0.01f) {                                                                   // real bloom: the line, blurred twice and added as light
                ComPtr<ID2D1CommandList> cl;
                if (rec && SUCCEEDED(rec->CreateCommandList(&cl))) {                            // record just this line (with its scale) on the side context
                    ComPtr<ID2D1LinearGradientBrush> gs = gradBrush(cA, cB, gx1, o.y, gx2, o.y, 1.f);
                    rec->SetTarget(cl.Get()); rec->BeginDraw(); rec->SetTransform(xf);
                    rec->DrawTextLayout(o, it.lay.Get(), gs.Get());
                    rec->EndDraw(); rec->SetTarget(nullptr);
                    cl->Close();
                    float k = S.lyricGlow * it.alpha * gw * (1.f + 0.35f * an.pulse);             // breathes with the beat, like the visual
                    float kS = 0.85f * k, kL = 0.65f * k;
                    lyBlurS->SetInput(0, cl.Get()); lyBlurL->SetInput(0, cl.Get());
                    lyBlurS->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 4.f * sc * it.s);
                    lyBlurL->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 16.f * sc * it.s);
                    lyMatS->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, D2D1::Matrix5x4F(kS, 0, 0, 0, 0, kS, 0, 0, 0, 0, kS, 0, 0, 0, 0, kS, 0, 0, 0, 0));
                    lyMatL->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, D2D1::Matrix5x4F(kL, 0, 0, 0, 0, kL, 0, 0, 0, 0, kL, 0, 0, 0, 0, kL, 0, 0, 0, 0));
                    rt->SetTransform(D2D1::Matrix3x2F::Identity());                             // the recording already holds the transform
                    ctx->DrawImage(lyMatL.Get(), nullptr, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_PLUS);
                    ctx->DrawImage(lyMatS.Get(), nullptr, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_PLUS);
                    lyBlurS->SetInput(0, nullptr); lyBlurL->SetInput(0, nullptr);
                    rt->SetTransform(xf);
                }
            }
        }
    }
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    if (lySide) {                                                                               // accent bar rides along with the reading point
        setc(cA, 1.f, 0.9f); fillRR(x0 - 18 * sc, lyAnchor - lyBarH * 0.5f + 2 * sc, x0 - 14 * sc, lyAnchor + lyBarH * 0.5f - 2 * sc, 2 * sc);
    }
}