// Settings, parameter table, presets, ini persistence
#pragma once
#include "common.h"

extern std::wstring gToastText;
extern double gToastT;
void toast(const std::wstring& s);
struct Settings {
    float mode = 0, theme = 0, colorBy = 0, colorSpeed = 0.5f, hue = 0, bright = 1.f;
    float bands = 96, gain = 1.f, tilt = 3.f, range = 70.f, fmin = 35.f, fmax = 17000.f, smooth = 0.35f;
    float barStyle = 0, gap = 0.25f, round = 0.6f, lineW = 1.f, mirror = 0, reflect = 1, caps = 1, rot = 0.15f, flow = 1.f;
    float bloom = 1.f, bloomSize = 1.f, trails = 0.35f, split = 0.4f, shake = 0.3f, flash = 0.15f, vignette = 0.35f;
    float particles = 1, partAmt = 1.f, partSize = 1.f, bgPulse = 0.5f, punch = 1.f, beatSens = 1.f;
    float centerArt = 1, artBg = 0.5f, nowPlaying = 0, autoCycle = 0, demo = 1, artMotion = 0.6f;
    float lyrics = 1, lyricSize = 1.f, lyricPos = 0, lyricGlow = 0.6f, lyricOffset = 0, lyricFont = 0, lyricColor = 0, lyricSide = 1, npSize = 0.8f, intro = 1;
    float panelAlpha = 0.35f;                                                       // see-through UI panels (0 = fully clear)
    float fx = 1, grain = 0.15f, streaks = 0.2f, grade = 0.5f;                      // GPU shader post-processing
    float bgStyle = 0, bgStrength = 0.5f;                                           // shader background (0 off, 1 Flow, 2 Nebula, 3 Aurora)
    float translate = 0, trLang = 0;                                                // lyric translation (DeepL) + target language index
};
extern Settings S;
struct Param { const wchar_t* name; float* v; float mn, mx, st; const wchar_t* const* labels; float def; };
extern const wchar_t* const kMode[];
extern const wchar_t* const kBgStyle[];
constexpr int NTRLANG = 17;
extern const wchar_t* const kTrLang[NTRLANG];   // "Translate to" names
extern const char* const kTrCode[NTRLANG];      // matching DeepL target_lang codes
const char* translateTarget();                  // DeepL code for the current setting, or "" when translation is off
extern const wchar_t* const kTheme[];
extern const wchar_t* const kColorBy[];
extern const wchar_t* const kBarStyle[];
extern const wchar_t* const kNowPlay[];
extern const wchar_t* const kLyFont[];
extern const wchar_t* const kLyColor[];
extern const wchar_t* const kLySide[];
extern const wchar_t* const kLyPos[];
extern const wchar_t* const kOnOff[];
extern Param P[];
extern const int NP;
void adjust(Param& p, int dir, bool big);
std::wstring iniPath();
bool loadSection(const std::wstring& sec);
void saveSection(const std::wstring& sec);
constexpr int NPRESET = 1;                      // built-in looks (F1 ...)
extern const wchar_t* const kPresetNames[NPRESET];
void applyPreset(int i);
extern bool gSlotUsed[4];                       // which of your 4 saved looks exist
void refreshSlots();
std::wstring exportCode();                      // whole look as a short shareable text code
bool importCode(const std::wstring& text);      // finds a code inside any pasted text