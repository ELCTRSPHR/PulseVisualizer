// Renderer: Bars / Radial visuals, particles, center art
#include "common.h"
#include "util.h"
#include "settings.h"
#include "color.h"
#include "analyzer.h"
#include "ui_state.h"
#include "renderer.h"

D2D1_COLOR_F Renderer::colorFor(int i, int B, float v) { return palette(theme, byLevel ? v : (B > 1 ? i / (float)(B - 1) : 0.f), phase); }
// ---- VISUAL 1: bars (neon tube / solid / LED) ----

void Renderer::drawBars(float punch) {
    int B = an.nb; bool mir = S.mirror > 0.5f; int style = (int)(S.barStyle + 0.5f);
    float slot = (float)W / ((float)B - S.gap), bw = std::max(1.f, slot * (1.f - S.gap)), rad = bw * 0.5f * S.round;      // first bar starts at x=0, last bar ends at x=W
    float base = mir ? H * 0.56f : H * 0.90f, maxH = mir ? H * 0.40f : H * 0.64f;
    baseY = base; maxHt = maxH;
    const float g = 0.55f;                                  // Bars cover a lot of area, so they are drawn dimmer than the other visuals (no white-out)
    float capH = std::max(2.f, bw * 0.35f);
    float segH = std::max(4.f * sc, maxH / 30.f); int totalSeg = (int)(maxH / segH);
    for (int i = 0; i < B; i++) {
        float v = an.val[i];
        float h = clampv(std::max(v * maxH * punch, bw * 0.5f + 1.f), 0.f, maxH * 1.1f);
        float x = i * slot;
        D2D1_COLOR_F c = colorFor(i, B, v);
        float top = base - h, bot = mir ? base + h : base;
        if (style == 2) {
            int n = (int)(h / segH);
            for (int k = 0; k < n; k++) {
                D2D1_COLOR_F ck = byLevel ? palette(theme, k / (float)totalSeg, phase) : c;
                setc(ck, 0.9f * g);
                float sy1 = base - k * segH, sy0 = sy1 - segH * 0.78f;
                fillRR(x, sy0, x + bw, sy1, rad * 0.4f);
                if (mir) fillRR(x, base + k * segH + segH * 0.22f, x + bw, base + (k + 1) * segH, rad * 0.4f);
            }
        }
        else {
            setc(c, (style == 1 ? 1.f : 0.85f) * g); fillRR(x, top, x + bw, bot, rad);
            if (style == 0 && bw > 5.f) { float in = bw * 0.30f; setc(whiten(c, 0.35f), 0.8f * g); fillRR(x + in, top, x + bw - in, bot, rad * 0.5f); }
        }
        if (!mir && S.reflect > 0.5f) { setc(c, g, 0.16f); float ry = base + 4.f * sc; fillRR(x, ry, x + bw, ry + h * 0.4f, rad); }
        if (S.caps > 0.5f) {
            float ph = an.peak[i] * maxH * punch;
            if (ph > h + capH) {
                setc(whiten(c, 0.4f), g);
                fillRR(x, base - ph - capH * 1.6f, x + bw, base - ph - capH * 0.6f, rad * 0.5f);
                if (mir) fillRR(x, base + ph + capH * 0.6f, x + bw, base + ph + capH * 1.6f, rad * 0.5f);
            }
        }
    }
}
// ---- VISUAL 2: radial burst ----

void Renderer::drawRadial(float punch) {
    int B = an.nb, count = B * 2; float m = (float)std::min(W, H);
    float cx = W * 0.5f - radShift * W * 0.17f, cy = H * 0.5f; artCx = cx;
    float r0 = m * 0.16f * (1.f + 0.10f * an.bass * S.punch + 0.14f * an.pulse * S.punch), maxL = m * 0.30f;
    float lw = std::max(1.5f * sc, (2.f * PI * r0 / count) * 0.5f * S.lineW);
    bool inward = S.mirror > 0.5f;
    artR = r0 - lw * 3.4f;
    for (int i = 0; i < count; i++) {
        int idx = i < B ? i : count - 1 - i;
        float v = an.val[idx], a = rotAngle + (float)i / count * 2.f * PI - PI * 0.5f;
        float dx = cosf(a), dy = sinf(a), len = v * maxL * punch + 2.f * sc;
        D2D1_COLOR_F c = colorFor(idx, B, v);
        setc(c, 0.9f);
        rt->DrawLine(D2D1::Point2F(cx + dx * r0, cy + dy * r0), D2D1::Point2F(cx + dx * (r0 + len), cy + dy * (r0 + len)), br.Get(), lw, roundStroke.Get());
        if (inward) {
            float ri = r0 - lw * 2.f, l2 = std::min(len * 0.4f, ri * 0.9f);
            setc(c, 0.55f);
            rt->DrawLine(D2D1::Point2F(cx + dx * ri, cy + dy * ri), D2D1::Point2F(cx + dx * (ri - l2), cy + dy * (ri - l2)), br.Get(), lw, roundStroke.Get());
        }
        if (S.caps > 0.5f && an.peak[idx] > v + 0.03f) {
            float pr = r0 + an.peak[idx] * maxL * punch + lw * 1.6f;
            setc(whiten(c, 0.6f));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + dx * pr, cy + dy * pr), lw * 0.5f, lw * 0.5f), br.Get());
        }
    }
    setc(palette(theme, 0.5f, phase), 1.f, 0.8f);
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r0 - lw * 1.8f, r0 - lw * 1.8f), br.Get(), (1.5f + an.bass * 5.f) * sc, roundStroke.Get());
    setc(palette(theme, 0.1f, phase), 1.f, 0.35f);
    float r2 = r0 * (0.78f - 0.07f * an.pulse);
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r2, r2), br.Get(), 1.5f * sc, roundStroke.Get());
}
// ---- NCS (homage): a thick neon ring whose inner edge is pushed in by the music, around a slowly turning dotted sphere ----
void Renderer::drawNCS(float punch) {
    const int B = an.nb; if (B < 2) return;
    const float m = (float)std::min(W, H);
    const float cx = W * 0.5f - radShift * W * 0.17f, cy = H * 0.5f;
    const float R = m * 0.36f * (1.f + 0.035f * an.bass * S.punch + 0.05f * an.pulse * S.punch);   // outer edge: a clean circle that thumps on the kick
    static float spin = 0.f; spin += uiDt * (0.18f + 0.9f * an.bass);
    // spectrum wrapped around the ring (mirrored so it flows smoothly all the way round), heavily smoothed -> soft, blobby bulges
    auto sample = [&](const float* v, float u) {
        float f = clampv(u, 0.f, 1.f) * (B - 1); int i = (int)f; float fr = f - i; int j = std::min(i + 1, B - 1);
        return v[i] * (1.f - fr) + v[j] * fr;
        };
    auto soft = [&](const float* v, float u) {
        float acc = 0.f, wsum = 0.f;
        for (int k = -4; k <= 4; k++) { float w = 1.f - fabsf((float)k) / 5.f; acc += w * sample(v, u + k * 0.018f); wsum += w; }
        return acc / wsum;
        };
    const float rot = spin * 0.15f;
    auto thick = [&](const float* v, float t) {                       // ring thickness at angle t (0..1)
        float u = t < 0.5f ? t * 2.f : (1.f - t) * 2.f;
        u = 0.08f + 0.80f * u;                                            // skip the very lowest / highest bins: they barely move
        return R * (0.05f + 0.30f * clampv(soft(v, u) * punch, 0.f, 1.2f));
        };
    const int N = 200;
    D2D1_COLOR_F col = palette(theme, 0.5f, phase), core = whiten(col, 0.25f);

    // ---- the dotted sphere inside ----
    {
        const float Rs = R * 0.80f;
        const int rows = 24, cols = 64;
        const float tilt = 0.42f + 0.06f * sinf(spin * 0.4f), ct = cosf(tilt), st = sinf(tilt);
        const float ds = std::max(1.1f, 1.6f * sc);
        for (int i = 1; i < rows; i++) {
            float th = PI * (float)i / rows, sth = sinf(th), cth = cosf(th);
            float bv = sample(an.val, 0.05f + 0.85f * (float)i / rows);     // each latitude ring breathes with its own band
            for (int j = 0; j < cols; j++) {
                float ph = 2.f * PI * (float)j / cols + spin;
                float wave = 1.f + 0.10f * bv * punch * sinf(3.f * ph - spin * 2.f) + 0.05f * an.bass;
                float X = Rs * wave * sth * cosf(ph), Y = Rs * wave * cth, Z = Rs * wave * sth * sinf(ph);
                float Y2 = Y * ct - Z * st, Z2 = Y * st + Z * ct;           // tilt toward the viewer
                float d = Z2 / Rs;                                           // -1 back .. +1 front
                if (d < -0.15f) continue;                                    // hide the far side, like a lit shell
                float a = clampv(0.25f + 0.75f * d, 0.f, 1.f), r = ds * (0.55f + 0.6f * clampv(d, 0.f, 1.f));
                setc(col, 0.8f, a * (0.35f + 0.30f * bv));
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + X, cy + Y2), r, r), br.Get());
            }
        }
    }

    // ---- the ring: outer circle minus the music-shaped inner edge ----
    auto innerPt = [&](const float* v, int k, float extra) {
        float t = (float)(k % N) / N, a = PI * 0.5f + t * 2.f * PI + rot;
        float rr = R - thick(v, t) - extra;
        return D2D1::Point2F(cx + cosf(a) * rr, cy + sinf(a) * rr);
        };
    {
        ComPtr<ID2D1PathGeometry> pg;
        if (SUCCEEDED(factory->CreatePathGeometry(&pg))) {
            ComPtr<ID2D1GeometrySink> sk;
            if (SUCCEEDED(pg->Open(&sk))) {
                sk->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
                sk->BeginFigure(D2D1::Point2F(cx + R, cy), D2D1_FIGURE_BEGIN_FILLED);
                sk->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx - R, cy), D2D1::SizeF(R, R), 0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sk->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + R, cy), D2D1::SizeF(R, R), 0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sk->EndFigure(D2D1_FIGURE_END_CLOSED);
                for (int k = 0; k <= N; k++) { D2D1_POINT_2F p = innerPt(an.val, k, 0.f); if (k == 0) sk->BeginFigure(p, D2D1_FIGURE_BEGIN_FILLED); else sk->AddLine(p); }
                sk->EndFigure(D2D1_FIGURE_END_CLOSED);
                sk->Close();
                setc(col, 0.85f, 0.55f);
                rt->FillGeometry(pg.Get(), br.Get());
            }
        }
    }
    // thin trailing inner edges (older spectra) give the ring its layered, striated inner wall
    for (int L = 1; L <= 4; L++) {
        const float* hv = hist[(histHead - L * 2 + HIST_N * 8) % HIST_N];
        ComPtr<ID2D1PathGeometry> pg; if (FAILED(factory->CreatePathGeometry(&pg))) continue;
        ComPtr<ID2D1GeometrySink> sk; if (FAILED(pg->Open(&sk))) continue;
        for (int k = 0; k <= N; k++) { D2D1_POINT_2F p = innerPt(hv, k, L * 1.5f * sc); if (k == 0) sk->BeginFigure(p, D2D1_FIGURE_BEGIN_HOLLOW); else sk->AddLine(p); }
        sk->EndFigure(D2D1_FIGURE_END_CLOSED); sk->Close();
        setc(col, 0.8f, 0.18f - 0.03f * L);
        rt->DrawGeometry(pg.Get(), br.Get(), 1.2f * sc);
    }
    // hot core along the outer rim + a soft outer halo (the neon glow)
    setc(core, 0.8f, 0.22f + 0.15f * an.pulse);
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), R - 2.f * sc, R - 2.f * sc), br.Get(), 3.f * sc);
    for (int g = 1; g <= 3; g++) {
        setc(col, 0.8f, 0.045f / g);
        rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), R + g * 5.f * sc, R + g * 5.f * sc), br.Get(), 8.f * sc);
    }
}
// ---- particles ----

void Renderer::updateParticles(float dt) {
    if (S.particles < 0.5f) { parts.clear(); return; }
    int mode = (int)(S.mode + 0.5f);
    float rate = S.partAmt * (25.f + 260.f * an.treble + 120.f * an.level);
    partAcc += rate * dt; int n = (int)partAcc; partAcc -= n;
    if (an.beat) n += (int)(45 * S.partAmt);
    float m = (float)std::min(W, H);
    for (int i = 0; i < n && parts.size() < 2500; i++) {
        Particle p; p.t = frand(); p.size = (1.5f + 3.5f * frand()) * sc * S.partSize; p.maxLife = p.life = 0.8f + 1.6f * frand();
        if (mode == 2) {                                                     // NCS: sparse dust drifting out from behind the ring, faster on the bass
            if (frand() < 0.6f) continue;
            float a = frand() * 2.f * PI, rr = m * (0.38f + 0.35f * frand()), sp = (40.f + 160.f * frand()) * sc * (1.f + 2.5f * an.bass + an.pulse);
            p.x = W * 0.5f - radShift * W * 0.17f + cosf(a) * rr; p.y = H * 0.5f + sinf(a) * rr; p.vx = cosf(a) * sp; p.vy = sinf(a) * sp;
            p.size *= 0.6f; p.maxLife = p.life = 1.5f + 2.5f * frand();
        }
        else if (mode == 1) {                                                // Radial: burst out of the ring
            float a = frand() * 2.f * PI, sp = (120.f + 380.f * frand()) * sc * (1.f + an.pulse), r0 = m * 0.16f;
            p.x = W * 0.5f - radShift * W * 0.17f + cosf(a) * r0; p.y = H * 0.5f + sinf(a) * r0; p.vx = cosf(a) * sp; p.vy = sinf(a) * sp;
        }
        else {                                                              // Bars: sparks off the bar tops
            int bi = clampv((int)(frand() * an.nb), 0, an.nb - 1);
            p.x = (bi + 0.5f) * ((float)W / an.nb);
            p.y = baseY - an.val[bi] * maxHt; p.vx = (frand() - 0.5f) * 60.f * sc; p.vy = -(60.f + 260.f * frand()) * sc;
        }
        parts.push_back(p);
    }
    for (size_t i = 0; i < parts.size();) {
        Particle& p = parts[i]; p.life -= dt;
        if (p.life <= 0.f) { parts[i] = parts.back(); parts.pop_back(); continue; }
        p.x += p.vx * dt; p.y += p.vy * dt; p.vx *= expf(-dt * 0.5f); p.vy *= expf(-dt * 0.5f); i++;
    }
}
void Renderer::drawParticles() {
    for (auto& p : parts) {
        float a = p.life / p.maxLife, r = p.size * (0.4f + 0.6f * a);
        setc(palette(theme, p.t, phase), 1.f, a * 0.9f);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(p.x, p.y), r, r), br.Get());
    }
}
// ---- album art backdrop + spinning record in the middle (Radial) ----

void Renderer::drawArtBackdrop(float dt) {
    const float mot = S.artMotion;
    artDrift += dt * (0.35f + 0.9f * an.bass) * (0.2f + mot);
    float zoom = 1.12f + mot * (0.05f * sinf(artDrift * 0.9f) + 0.05f * an.bass + 0.04f * an.pulse);      // breathing + beat kick
    float ang = mot * 0.05f * sinf(artDrift * 0.5f);                                                       // slow sway
    float px = mot * W * 0.025f * sinf(artDrift * 0.7f), py = mot * H * 0.025f * cosf(artDrift * 0.6f);    // slow drift
    float k = S.artBg * (0.42f + mot * (0.22f * an.bass + 0.30f * an.pulse));                              // brightness follows the beat
    const float sat = 1.4f, lr = 0.3086f, lg = 0.6094f, lb = 0.0820f, is = 1.f - sat;
    D2D1_MATRIX_5X4_F m = D2D1::Matrix5x4F(k * (lr * is + sat), k * lr * is, k * lr * is, 0.f,
        k * lg * is, k * (lg * is + sat), k * lg * is, 0.f,
        k * lb * is, k * lb * is, k * (lb * is + sat), 0.f,
        0.f, 0.f, 0.f, 1.f,
        0.f, 0.f, 0.f, 0.f);
    auto layer = [&](ID2D1Effect* mat, int aw, int ah, float opacity, D2D1_COMPOSITE_MODE mode) {
        mat->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, m);
        float s = std::max(W / (float)aw, H / (float)ah) * zoom;
        D2D1_MATRIX_3X2_F old; ctx->GetTransform(&old);
        bool fade = opacity < 0.999f;
        if (fade) ctx->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity), nullptr);
        ctx->SetTransform(D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(-aw * s * 0.5f, -ah * s * 0.5f) *
            D2D1::Matrix3x2F::Rotation(ang * 180.f / PI, D2D1::Point2F(0.f, 0.f)) * D2D1::Matrix3x2F::Translation(W * 0.5f + px, H * 0.5f + py));
        ctx->DrawImage(mat, nullptr, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, mode);
        ctx->SetTransform(old);
        if (fade) ctx->PopLayer();
        };
    const D2D1_COMPOSITE_MODE cm = bgShader ? D2D1_COMPOSITE_MODE_PLUS : D2D1_COMPOSITE_MODE_SOURCE_OVER;   // over a shader background: add, don't cover it
    if (artPrev) layer(artMatP.Get(), artPrevW, artPrevH, bgShader ? 1.f - smooth01(artFade) : 1.f, cm);
    if (art) layer(artMat.Get(), artW, artH, artPrev ? smooth01(artFade) : 1.f, cm);
    if (mot > 0.01f) {                                                       // color wash that swells on the beat
        D2D1_COLOR_F c0 = D2D1::ColorF(gAlbumCur[1][0], gAlbumCur[1][1], gAlbumCur[1][2], S.artBg * mot * (0.05f + 0.20f * an.pulse + 0.10f * an.bass));
        D2D1_COLOR_F c1 = c0; c1.a = 0.f;
        D2D1_GRADIENT_STOP gs[2] = { { 0.f, c0 },{ 1.f, c1 } };
        ComPtr<ID2D1GradientStopCollection> coll; ComPtr<ID2D1RadialGradientBrush> rb;
        rt->CreateGradientStopCollection(gs, 2, &coll);
        rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(W * 0.5f, H * 0.5f), D2D1::Point2F(0, 0), W * 0.55f, H * 0.65f), coll.Get(), &rb);
        if (rb) rt->FillRectangle(D2D1::RectF(0, 0, (float)W, (float)H), rb.Get());
    }
}
void Renderer::drawCenterArt() {
    if (!art || !artBrush || S.centerArt < 0.5f || artR <= 1.f) return;
    float ra = smooth01(mixW[1]);                                              // the record belongs to Radial only (NCS has its dotted sphere)
    float spinK = (mixW[1] + mixW[2]) > 0.001f ? mixW[1] / (mixW[1] + mixW[2]) : 1.f;   // Radial spins the record, NCS keeps it still
    // the record keeps its own angle: it only advances by this frame's rotation (scaled by how "Radial" we are),
    // so leaving Radial gently slows it to a stop instead of unwinding all the turns it has ever made
    static float discDeg = 0.f, lastRot = 0.f; static bool discInit = false;
    if (!discInit) { lastRot = rotAngle; discInit = true; }
    float dRot = rotAngle - lastRot; lastRot = rotAngle;
    if (fabsf(dRot) < 1.f) discDeg = fmodf(discDeg + dRot * 0.5f * 180.f / PI * smooth01(spinK), 360.f);
    // switching Radial <-> NCS: the record does a deliberate whirl of one and a half turns, always forward,
    // eased in and out across the cross-fade (smootherstep), instead of a jerky unwind
    static float lastS = -1.f;
    float sS = spinK * spinK * spinK * (spinK * (spinK * 6.f - 15.f) + 10.f);
    if (lastS < 0.f) lastS = sS;
    discDeg = fmodf(discDeg + fabsf(sS - lastS) * 540.f, 360.f); lastS = sS;
    if (ra < 0.01f) return;
    bool fadeLayer = ra < 0.999f;
    if (fadeLayer) ctx->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), ra), nullptr);
    float ta = (float)(nowSec() - gTrackT);
    bool anim = ta >= 0.f && ta < 1.6f;                                      // track-change animation
    float e = clampv(ta / 0.75f, 0.f, 1.f);
    float r = artR * (anim ? 0.78f + 0.22f * easeBack(e) : 1.f);            // pop in with a little overshoot
    float spin = anim ? (1.f - e) * (1.f - e) * 80.f : 0.f;                 // extra spin that settles
    float cx = artCx, cy = H * 0.5f;
    auto disc = [&](ID2D1BitmapBrush* b, int aw, int ah, float opacity) {
        float scl = 2.f * r / aw;
        b->SetTransform(D2D1::Matrix3x2F::Scale(scl, scl) * D2D1::Matrix3x2F::Rotation(discDeg + spin, D2D1::Point2F(aw * scl * 0.5f, ah * scl * 0.5f)) * D2D1::Matrix3x2F::Translation(cx - r, cy - r));
        b->SetOpacity(opacity);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), b);
        b->SetOpacity(1.f);
        };
    if (artPrev && artBrushP) disc(artBrushP.Get(), artPrevW, artPrevH, 1.f);
    disc(artBrush.Get(), artW, artH, artPrev ? smooth01(artFade) : 1.f);
    if (anim && ta < 1.1f) {                                                 // shockwave ring
        float q = ta / 1.1f, qe = 1.f - (1.f - q) * (1.f - q), fa = (1.f - q) * (1.f - q);
        setc(palette(theme, 0.5f, phase), 1.f, 0.7f * fa);
        rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), artR * (1.f + 0.7f * qe), artR * (1.f + 0.7f * qe)), br.Get(), (0.8f + 4.f * (1.f - q)) * sc, roundStroke.Get());
    }
    setc(palette(theme, 0.5f, phase), 1.f, 0.9f * spinK);
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), br.Get(), 2.5f * sc, roundStroke.Get());
    br->SetColor(D2D1::ColorF(0.02f, 0.02f, 0.03f, 0.95f * spinK));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r * 0.07f, r * 0.07f), br.Get());
    if (fadeLayer) ctx->PopLayer();
}