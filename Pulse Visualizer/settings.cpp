// Settings, parameter table, presets, ini persistence
#include "common.h"
#include "util.h"
#include "settings.h"
#include "renderer.h"

// ============================================================================
//  TOASTS + SETTINGS
// ============================================================================
std::wstring gToastText;
double gToastT = -100;
void toast(const std::wstring& s) { gToastText = s; gToastT = nowSec(); }
Settings S;
const wchar_t* const kMode[] = { L"Bars", L"Radial", L"NCS" };
const wchar_t* const kBgStyle[] = { L"Off", L"Flow", L"Nebula", L"Aurora" };
const wchar_t* const kTheme[] = { L"RGB Rainbow", L"Neon", L"Inferno", L"Aurora", L"Sunset", L"Ice", L"Mono", L"Album art" };
const wchar_t* const kColorBy[] = { L"Frequency", L"Level" };
const wchar_t* const kBarStyle[] = { L"Neon tube", L"Solid", L"LED segments" };
const wchar_t* const kNowPlay[] = { L"Auto (hover)", L"Always full", L"Always compact", L"Off" };
const wchar_t* const kLyFont[] = { L"Modern", L"Display", L"Condensed", L"Serif italic" };
const wchar_t* const kLyColor[] = { L"Theme gradient", L"Clean white", L"Theme solid" };
const wchar_t* const kLySide[] = { L"Centered", L"Middle right" };
const wchar_t* const kLyPos[] = { L"Bottom", L"Center", L"Top" };
const wchar_t* const kOnOff[] = { L"Off", L"On" };
Param P[] = {
    { L"Visual",             &S.mode,       0,    2,      1,     kMode,     0 },
    { L"Color theme",        &S.theme,      0,    7,      1,     kTheme,    0 },
    { L"Color by",           &S.colorBy,    0,    1,      1,     kColorBy,  0 },
    { L"Color speed",        &S.colorSpeed, 0,    2,      0.05f, nullptr,   0 },
    { L"Hue offset",         &S.hue,        0,    1,      0.05f, nullptr,   0 },
    { L"Brightness",         &S.bright,     0.4f, 1.6f,   0.05f, nullptr,   0 },
    { L"Bands",              &S.bands,      24,   192,    8,     nullptr,   0 },
    { L"Sensitivity",        &S.gain,       0.3f, 4,      0.05f, nullptr,   0 },
    { L"Treble boost",       &S.tilt,       0,    9,      0.5f,  nullptr,   0 },
    { L"Dynamic range (dB)", &S.range,      40,   90,     5,     nullptr,   0 },
    { L"Min frequency (Hz)", &S.fmin,       20,   200,    5,     nullptr,   0 },
    { L"Max frequency (Hz)", &S.fmax,       4000, 20000,  500,   nullptr,   0 },
    { L"Smoothing",          &S.smooth,     0,    1,      0.05f, nullptr,   0 },
    { L"Bar style",          &S.barStyle,   0,    2,      1,     kBarStyle, 0 },
    { L"Bar gap",            &S.gap,        0,    0.8f,   0.05f, nullptr,   0 },
    { L"Roundness",          &S.round,      0,    1,      0.1f,  nullptr,   0 },
    { L"Line width",         &S.lineW,      0.3f, 3,      0.1f,  nullptr,   0 },
    { L"Mirror",             &S.mirror,     0,    1,      1,     kOnOff,    0 },
    { L"Reflection",         &S.reflect,    0,    1,      1,     kOnOff,    0 },
    { L"Peak caps",          &S.caps,       0,    1,      1,     kOnOff,    0 },
    { L"Rotation",           &S.rot,       -1,    1,      0.05f, nullptr,   0 },
    { L"Bloom",              &S.bloom,      0,    3,      0.1f,  nullptr,   0 },
    { L"Bloom size",         &S.bloomSize,  0.4f, 3,      0.1f,  nullptr,   0 },
    { L"Trails",             &S.trails,     0,    0.95f,  0.05f, nullptr,   0 },
    { L"RGB split",          &S.split,      0,    3,      0.1f,  nullptr,   0 },
    { L"Camera shake",       &S.shake,      0,    2,      0.1f,  nullptr,   0 },
    { L"Beat flash",         &S.flash,      0,    1,      0.05f, nullptr,   0 },
    { L"Vignette",           &S.vignette,   0,    1,      0.05f, nullptr,   0 },
    { L"Particles",          &S.particles,  0,    1,      1,     kOnOff,    0 },
    { L"Particle amount",    &S.partAmt,    0.2f, 3,      0.1f,  nullptr,   0 },
    { L"Particle size",      &S.partSize,   0.4f, 3,      0.1f,  nullptr,   0 },
    { L"Background pulse",   &S.bgPulse,    0,    1,      0.05f, nullptr,   0 },
    { L"Beat punch",         &S.punch,      0,    2,      0.1f,  nullptr,   0 },
    { L"Beat sensitivity",   &S.beatSens,   0.5f, 2,      0.1f,  nullptr,   0 },
    { L"Center album art",   &S.centerArt,  0,    1,      1,     kOnOff,    0 },
    { L"Album art backdrop", &S.artBg,      0,    1,      0.05f, nullptr,   0 },
    { L"Now playing card",   &S.nowPlaying, 0,    3,      1,     kNowPlay,  0 },
    { L"Now playing size",   &S.npSize,     0.5f, 1.3f,   0.05f, nullptr,   0 },
    { L"Auto-cycle (sec)",   &S.autoCycle,  0,    120,    5,     nullptr,   0 },
    { L"Lyrics",             &S.lyrics,     0,    1,      1,     kOnOff,    0 },
    { L"Lyrics size",        &S.lyricSize,  0.6f, 2,      0.1f,  nullptr,   0 },
    { L"Lyrics position",    &S.lyricPos,   0,    2,      1,     kLyPos,    0 },
    { L"Lyrics glow",        &S.lyricGlow,  0,    2,      0.1f,  nullptr,   0 },
    { L"Lyrics font",        &S.lyricFont,  0,    3,      1,     kLyFont,   0 },
    { L"Lyrics colors",      &S.lyricColor, 0,    2,      1,     kLyColor,  0 },
    { L"Lyrics layout",      &S.lyricSide, 0,    1,      1,     kLySide,   0 },
    { L"Lyrics offset (ms)", &S.lyricOffset,-3000,3000,   50,    nullptr,   0 },
    { L"Idle demo",          &S.demo,       0,    1,      1,     kOnOff,    0 },
    { L"Backdrop motion",    &S.artMotion,  0,    1,      0.05f, nullptr,   0 },
    { L"Song intro",         &S.intro,      0,    1,      1,     kOnOff,    0 },
    { L"Panel opacity",      &S.panelAlpha, 0,    0.8f,   0.05f, nullptr,   0 },
    { L"Shader effects",     &S.fx,         0,    1,      1,     kOnOff,    0 },
    { L"Film grain",         &S.grain,      0,    1,      0.05f, nullptr,   0 },
    { L"Bloom streaks",      &S.streaks,    0,    1,      0.05f, nullptr,   0 },
    { L"Color grade",        &S.grade,      0,    1,      0.05f, nullptr,   0 },
    { L"Shader background",  &S.bgStyle,    0,    3,      1,     kBgStyle,  0 },
    { L"Background strength",&S.bgStrength, 0,    1,      0.05f, nullptr,   0 },
};
const int NP = (int)(sizeof(P) / sizeof(P[0]));
void adjust(Param& p, int dir, bool big) {
    if (p.labels) {
        int n = (int)(p.mx - p.mn) + 1;
        int i = ((int)(*p.v + 0.5f) - (int)p.mn + dir) % n; if (i < 0) i += n;
        *p.v = p.mn + (float)i;
    }
    else {
        float v = *p.v + dir * p.st * (big ? 5.f : 1.f);
        v = roundf(v / p.st) * p.st;
        *p.v = clampv(v, p.mn, p.mx);
    }
}
std::wstring iniPath() {
    wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr, b, MAX_PATH);
    std::wstring s = b; return s.substr(0, s.find_last_of(L'\\') + 1) + L"rgbpulse.ini";
}
bool loadSection(const std::wstring& sec) {
    std::wstring path = iniPath(); bool any = false;
    for (int i = 0; i < NP; i++) {
        wchar_t b[64] = {};
        GetPrivateProfileStringW(sec.c_str(), P[i].name, L"", b, 64, path.c_str());
        if (b[0]) { *P[i].v = clampv((float)_wtof(b), P[i].mn, P[i].mx); any = true; }
    }
    return any;
}
void saveSection(const std::wstring& sec) {
    std::wstring path = iniPath();
    for (int i = 0; i < NP; i++) {
        wchar_t b[64]; swprintf_s(b, L"%.4f", *P[i].v);
        WritePrivateProfileStringW(sec.c_str(), P[i].name, b, path.c_str());
    }
}
// Built-in looks are stored as share codes: to add one, copy a look (Ctrl+C) and append its name + code here.
const wchar_t* const kPresetNames[NPRESET] = { L"Radial" };
static const wchar_t* const kPresetCodes[NPRESET] = {
    L"PV1-OQEHAAoADBUOBgYDGgoCBQYGAQEBFxAIBgQAAAMBBwYDAAUBCgAFAAEDAQoAAAE8AAwBBwEDCAoACps",
};
void applyPreset(int i) {
    if (i < 0 || i >= NPRESET) return;
    for (int k = 0; k < NP; k++) *P[k].v = P[k].def;   // settings newer than the code fall back to their defaults
    S.flow = 1.f;
    importCode(kPresetCodes[i]);
    toast(std::wstring(L"Look: ") + kPresetNames[i]);
}

// ---- saved slots + shareable look codes ----
bool gSlotUsed[4] = {};
void refreshSlots() {
    std::wstring path = iniPath();
    for (int i = 0; i < 4; i++) {
        wchar_t b[64] = {}; std::wstring sec = L"slot" + std::to_wstring(i + 1);
        GetPrivateProfileStringW(sec.c_str(), P[0].name, L"", b, 64, path.c_str());
        gSlotUsed[i] = b[0] != 0;
    }
}
static const char* kCodeTag = "PV1-";
static bool b64Dec(const std::string& s, std::vector<uint8_t>& out) {
    uint32_t acc = 0; int bits = 0;
    for (char c : s) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A'; else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52; else if (c == '-' || c == '+') v = 62;
        else if (c == '_' || c == '/') v = 63; else if (c == '=') continue; else return false;
        acc = (acc << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((uint8_t)((acc >> bits) & 0xFF)); }
    }
    return true;
}
static uint8_t codeCheck(const std::vector<uint8_t>& d, size_t n) { uint8_t ck = 0x5A; for (size_t i = 0; i < n; i++) ck = (uint8_t)((ck * 31 + d[i]) & 0xFF); return ck; }
std::wstring exportCode() {
    std::vector<uint8_t> d; d.push_back((uint8_t)NP);
    for (int i = 0; i < NP; i++) {
        const Param& p = P[i]; float stp = p.st > 0.f ? p.st : 1.f;
        long q = lroundf((clampv(*p.v, p.mn, p.mx) - p.mn) / stp);
        unsigned u = (unsigned)std::max(0L, q);
        while (u >= 128) { d.push_back((uint8_t)(u | 128)); u >>= 7; }
        d.push_back((uint8_t)u);
    }
    d.push_back(codeCheck(d, d.size()));
    return toW(std::string(kCodeTag) + b64url(d));
}
bool importCode(const std::wstring& text) {
    std::string s = toU8(text); size_t p = s.find(kCodeTag); if (p == std::string::npos) return false;
    size_t b = p + 4, e = b; while (e < s.size() && (isalnum((unsigned char)s[e]) || s[e] == '-' || s[e] == '_')) e++;
    std::vector<uint8_t> d; if (!b64Dec(s.substr(b, e - b), d) || d.size() < 3) return false;
    if (codeCheck(d, d.size() - 1) != d.back()) return false;
    size_t pos = 0, end = d.size() - 1; int n = d[pos++]; if (n < 1) return false;
    std::vector<unsigned> vals;
    for (int i = 0; i < n; i++) {
        unsigned u = 0; int sh = 0;
        for (;;) { if (pos >= end || sh > 28) return false; uint8_t c = d[pos++]; u |= (unsigned)(c & 127) << sh; if (!(c & 128)) break; sh += 7; }
        vals.push_back(u);
    }
    for (int i = 0; i < std::min(n, NP); i++) { const Param& q = P[i]; float stp = q.st > 0.f ? q.st : 1.f; *q.v = clampv(q.mn + vals[i] * stp, q.mn, q.mx); }
    return true;
}