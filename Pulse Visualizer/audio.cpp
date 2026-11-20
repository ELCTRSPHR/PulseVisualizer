// WASAPI loopback capture
#include "common.h"
#include "util.h"
#include "audio.h"

AudioCapture::AudioCapture() : ring(RING, 0.f) {}
void AudioCapture::start() { run = true; th = std::thread([this] { loop(); }); }
void AudioCapture::stop() { run = false; if (th.joinable()) th.join(); }
void AudioCapture::restart() { restartFlag = true; }
int  AudioCapture::rate() const { return sampleRate; }
void AudioCapture::snapshot(float* out, size_t n) {
    std::lock_guard<std::mutex> l(m);
    size_t pos = (wpos + RING - n) % RING;
    for (size_t i = 0; i < n; i++) out[i] = ring[(pos + i) % RING];
}
void AudioCapture::push(float mo) { ring[wpos] = mo; wpos = (wpos + 1) % RING; }
void AudioCapture::loop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (run) {
        restartFlag = false;
        ComPtr<IMMDeviceEnumerator> en; ComPtr<IMMDevice> dev;
        ComPtr<IAudioClient> cl; ComPtr<IAudioCaptureClient> cap;
        WAVEFORMATEX* fmt = nullptr;
        bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))
            && SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))
            && SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)cl.GetAddressOf()))
            && SUCCEEDED(cl->GetMixFormat(&fmt))
            && SUCCEEDED(cl->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, fmt, nullptr))
            && SUCCEEDED(cl->GetService(IID_PPV_ARGS(&cap)))
            && SUCCEEDED(cl->Start());
        if (!ok) { if (fmt) CoTaskMemFree(fmt); Sleep(500); continue; }

        sampleRate = (int)fmt->nSamplesPerSec;
        const int ch = fmt->nChannels, bits = fmt->wBitsPerSample, align = fmt->nBlockAlign;
        WORD tag = fmt->wFormatTag;
        if (tag == WAVE_FORMAT_EXTENSIBLE) tag = (WORD)((WAVEFORMATEXTENSIBLE*)fmt)->SubFormat.Data1;
        const bool isFloat = (tag == WAVE_FORMAT_IEEE_FLOAT);

        auto rd = [&](const BYTE* p, int c) -> float {
            if (isFloat && bits == 32) return ((const float*)p)[c];
            if (bits == 16) return ((const int16_t*)p)[c] / 32768.f;
            if (bits == 24) { const BYTE* q = p + c * 3; int v = (q[0] << 8) | (q[1] << 16) | (q[2] << 24); return (v >> 8) / 8388608.f; }
            if (bits == 32) return ((const int32_t*)p)[c] / 2147483648.f;
            return 0.f;
            };
        double lastData = nowSec(), lastTick = lastData;
        while (run && !restartFlag) {
            Sleep(3);
            bool got = false, bad = false; UINT32 n = 0;
            for (;;) {
                if (FAILED(cap->GetNextPacketSize(&n))) { bad = true; break; }
                if (n == 0) break;
                BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) { bad = true; break; }
                {
                    std::lock_guard<std::mutex> l(m);
                    const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                    for (UINT32 f = 0; f < frames; f++) {
                        float mo = 0.f;
                        if (!silent) {
                            const BYTE* p = data + (size_t)f * align;
                            for (int c = 0; c < ch; c++) mo += rd(p, c);
                            mo /= (float)ch;
                        }
                        push(mo);
                    }
                }
                cap->ReleaseBuffer(frames);
                got = true;
            }
            if (bad) break;
            double t = nowSec();
            if (got) lastData = t;
            else if (t - lastData > 0.05) {      // loopback sends nothing during silence -> feed zeros
                size_t z = (size_t)std::min((t - lastTick) * sampleRate, (double)RING / 2);
                std::lock_guard<std::mutex> l(m);
                for (size_t i = 0; i < z; i++) push(0.f);
            }
            lastTick = t;
        }
        cl->Stop(); CoTaskMemFree(fmt);
    }
    CoUninitialize();
}
AudioCapture au;