// WASAPI loopback capture
#pragma once
#include "common.h"

// ============================================================================
//  AUDIO CAPTURE  (WASAPI loopback, mixed down to mono)
// ============================================================================
class AudioCapture {
public:
    AudioCapture();
    void start();
    void stop();
    void restart();
    int  rate() const;
    void snapshot(float* out, size_t n);
private:
    std::thread th;
    std::mutex m;
    std::vector<float> ring;
    size_t wpos = 0;
    std::atomic<bool> run{ false }, restartFlag{ false };
    std::atomic<int> sampleRate{ 48000 };
    void push(float mo);
    void loop();
};

extern AudioCapture au;