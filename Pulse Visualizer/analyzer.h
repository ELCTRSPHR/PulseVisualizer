// FFT analyzer, band smoothing, beat detection
#pragma once
#include "common.h"

// ============================================================================
//  ANALYZER
// ============================================================================
struct Analyzer {
    std::vector<float> win, buf, mag;
    std::vector<std::complex<float>> cx, tw;
    float val[MAXB] = {}, peak[MAXB] = {}, pvel[MAXB] = {};
    int nb = 96;
    float bass = 0, mid = 0, treble = 0, level = 0, pulse = 0, bassAvg = 0, sinceBeat = 1, silentT = 0, demoAmt = 0, tdemo = 0;
    bool beat = false;
    Analyzer();
    void fft();
    void update(float dt);
};

extern Analyzer an;