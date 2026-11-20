// FFT analyzer, band smoothing, beat detection
#include "common.h"
#include "settings.h"
#include "audio.h"
#include "analyzer.h"

Analyzer::Analyzer() {
    win.resize(FFT_N); buf.resize(FFT_N); mag.resize(FFT_N / 2 + 2); cx.resize(FFT_N); tw.resize(FFT_N / 2);
    for (int i = 0; i < FFT_N; i++) win[i] = 0.5f * (1.f - cosf(2.f * PI * i / (FFT_N - 1)));
    for (int k = 0; k < FFT_N / 2; k++) tw[k] = std::complex<float>(cosf(-2.f * PI * k / FFT_N), sinf(-2.f * PI * k / FFT_N));
}
void Analyzer::fft() {
    const int N = FFT_N;
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
        if (i < j) std::swap(cx[i], cx[j]);
    }
    for (int len = 2; len <= N; len <<= 1) {
        int half = len >> 1, step = N / len;
        for (int i = 0; i < N; i += len)
            for (int j = 0; j < half; j++) {
                std::complex<float> u = cx[i + j], v = cx[i + j + half] * tw[j * step];
                cx[i + j] = u + v; cx[i + j + half] = u - v;
            }
    }
}
void Analyzer::update(float dt) {
    au.snapshot(buf.data(), FFT_N);
    for (int i = 0; i < FFT_N; i++) cx[i] = std::complex<float>(buf[i] * win[i], 0.f);
    fft();
    for (int k = 0; k <= FFT_N / 2; k++) mag[k] = 4.f * std::abs(cx[k]) / FFT_N;
    mag[FFT_N / 2 + 1] = mag[FFT_N / 2];

    float rate = (float)au.rate(), binHz = rate / FFT_N;
    int B = clampv((int)(S.bands + 0.5f), 8, MAXB); nb = B;
    float fmin = S.fmin, fmax = std::min(S.fmax, rate * 0.48f); if (fmax < fmin * 2.f) fmax = fmin * 2.f;
    float gdb = 20.f * log10f(std::max(0.01f, S.gain));
    float floorDb = -(15.f + S.range);
    float aA = 1.f - expf(-dt / 0.010f), aR = 1.f - expf(-dt / (0.03f + S.smooth * 0.32f));
    float bs = 0, bc = 0, ms = 0, mc = 0, ts = 0, tcn = 0, all = 0, bf = 0, bfc = 0;

    for (int b = 0; b < B; b++) {
        float f0 = fmin * powf(fmax / fmin, (float)b / B), f1 = fmin * powf(fmax / fmin, (float)(b + 1) / B);
        float fc = sqrtf(f0 * f1);
        float k0 = f0 / binHz, k1 = f1 / binHz, m = 0.f;
        if (k1 - k0 < 1.f) { float kc = fc / binHz; int ki = (int)kc; float fr = kc - ki; m = mag[ki] * (1.f - fr) + mag[ki + 1] * fr; }
        else { int a0 = (int)ceilf(k0), a1 = std::min((int)floorf(k1), FFT_N / 2); for (int k = a0; k <= a1; k++) m = std::max(m, mag[k]); }
        float db = 20.f * log10f(m + 1e-7f) + gdb + S.tilt * log2f(fc / 1000.f);
        float t = powf(clampv((db - floorDb) / S.range, 0.f, 1.f), 1.5f);
        float cur = val[b]; val[b] = cur + (t - cur) * (t > cur ? aA : aR);
        if (val[b] >= peak[b]) { peak[b] = val[b]; pvel[b] = 0.f; }
        else { pvel[b] += 2.5f * dt; peak[b] = std::max(val[b], peak[b] - pvel[b] * dt); }
        all += t;
        if (fc < 250.f) { bs += t; bc++; }
        else if (fc < 4000.f) { ms += t; mc++; }
        else { ts += t; tcn++; }
        if (fc < 160.f) { bf += t; bfc++; }
    }
    auto sm = [&](float& cur, float t) { cur += (t - cur) * (t > cur ? aA : aR); };
    sm(bass, bc ? bs / bc : 0.f); sm(mid, mc ? ms / mc : 0.f); sm(treble, tcn ? ts / tcn : 0.f); sm(level, all / B);

    bf = bfc ? bf / bfc : 0.f;
    sinceBeat += dt; beat = false;
    bassAvg += (bf - bassAvg) * (1.f - expf(-dt / 0.9f));
    float thr = 1.f + 0.25f / std::max(0.3f, S.beatSens);
    if (bf > bassAvg * thr + 0.04f && bf > 0.25f && sinceBeat > 0.17f) { beat = true; pulse = 1.f; sinceBeat = 0.f; }

    // idle demo: when nothing is playing, keep the screen alive
    silentT = (level < 0.01f) ? silentT + dt : 0.f;
    float want = (S.demo > 0.5f && silentT > 3.f) ? 1.f : 0.f;
    demoAmt += (want - demoAmt) * (1.f - expf(-dt * 2.f));
    if (demoAmt > 0.01f) {
        float prev = tdemo; tdemo += dt;
        for (int b = 0; b < B; b++) {
            float x = (float)b / B;
            float d = demoAmt * (0.10f + 0.40f * (1.f - x * 0.7f) * (0.55f + 0.45f * sinf(tdemo * 1.9f + x * 9.f)) * (0.6f + 0.4f * sinf(tdemo * 0.7f - x * 3.f)));
            val[b] = std::max(val[b], d);
        }
        bass = std::max(bass, demoAmt * 0.45f * (0.5f + 0.5f * sinf(tdemo * 3.1f)));
        if (demoAmt > 0.5f && floorf(prev / 0.62f) != floorf(tdemo / 0.62f)) { pulse = 1.f; beat = true; }
    }
    else tdemo = 0.f;
    pulse *= expf(-dt * 5.f);
}
Analyzer an;