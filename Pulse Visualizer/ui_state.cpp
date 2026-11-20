// UI state, settings-panel model, clipboard, splash timing
#include "common.h"
#include "util.h"
#include "settings.h"
#include "spotify.h"
#include "ui_state.h"
#include "renderer.h"

std::vector<Hit> gHits;
HWND  gHwnd = nullptr;
bool  gShowUI = true, gPanel = false, gFullscreen = false, gMinimized = false;
int   gPendW = 0, gPendH = 0, gOverlay = OV_NONE, gDrag = 0, gVol = 50;
double gLastInput = 0, gTrackT = -100, gVolUntil = 0, gSeekUntil = 0;
float gSeekFrac = 0, gProgX0 = 0, gProgX1 = 1, gVolX0 = 0, gVolX1 = 1;
std::wstring gInput;
double gSplashT0 = -1;
float smooth01(float x) { x = clampv(x, 0.f, 1.f); return x * x * (3.f - 2.f * x); }
float easeBack(float x) { x = clampv(x, 0.f, 1.f); float u = x - 1.f; return 1.f + 2.70158f * u * u * u + 1.70158f * u * u; }
bool gSplashDone = false;
void skipSplash() {                                   // any key / click jumps straight to the fade-out
    double n = nowSec(), e = n - gSplashT0;
    if (gSplashT0 > 0 && e > 0.5 && e < gSplashDur - gSplashOut - 0.05) gSplashT0 = n - (gSplashDur - gSplashOut);
}
std::wstring clipGet() {
    std::wstring r;
    if (OpenClipboard(gHwnd)) {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) { const wchar_t* p = (const wchar_t*)GlobalLock(h); if (p) { r = p; GlobalUnlock(h); } }
        CloseClipboard();
    }
    return r;
}
void clipSet(const std::wstring& s) {
    if (!OpenClipboard(gHwnd)) return;
    EmptyClipboard();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    if (g) { wchar_t* p = (wchar_t*)GlobalLock(g); memcpy(p, s.c_str(), (s.size() + 1) * sizeof(wchar_t)); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT, g); }
    CloseClipboard();
}
// ---- settings panel: tab layout + shortcuts page ----
const wchar_t* const kPTabNames[7] = { L"Look", L"Color", L"Effects", L"Audio", L"Spotify", L"Presets", L"Keys" };
const wchar_t* const kPTabItems[5][20] = {
    { L"Visual", L"Bands", L"Bar style", L"Bar gap", L"Roundness", L"Line width", L"Mirror", L"Reflection", L"Peak caps", L"Rotation", L"Auto-cycle (sec)", nullptr },
    { L"Color theme", L"Color by", L"Color speed", L"Hue offset", L"Brightness", L"Center album art", L"Album art backdrop", L"Backdrop motion", L"Shader background", L"Background strength", L"Panel opacity", nullptr },
    { L"Shader effects", L"Bloom", L"Bloom size", L"Bloom streaks", L"Color grade", L"Film grain", L"Trails", L"RGB split", L"Camera shake", L"Beat flash", L"Vignette", L"Background pulse", L"Particles", L"Particle amount", L"Particle size", nullptr },
    { L"Sensitivity", L"Treble boost", L"Dynamic range (dB)", L"Min frequency (Hz)", L"Max frequency (Hz)", L"Smoothing", L"Beat punch", L"Beat sensitivity", L"Idle demo", nullptr },
    { L"Now playing card", L"Now playing size", L"Song intro", L"Lyrics", L"Lyrics layout", L"Lyrics position", L"Lyrics size", L"Lyrics font", L"Lyrics colors", L"Lyrics glow", L"Lyrics offset (ms)", nullptr },
};
const Shortcut kShortcuts[] = {
    { L"Tab   Esc",              L"Open / close settings" },
    { L"PgUp  PgDn   (Q  E)",    L"Switch settings tab" },
    { L"Up  Down",               L"Pick a setting  (volume when closed)" },
    { L"Left  Right",            L"Change a setting  (prev / next track)" },
    { L"Shift + Left / Right",   L"Change a setting in big steps" },
    { L"Backspace  Right-click", L"Reset the picked setting" },
    { L"Ctrl + C",               L"Copy your look as a share code" },
    { L"Ctrl + V",               L"Paste a share code" },
    { L"1  2  3",                L"Bars / Radial / NCS" },
    { L"C   Shift + C",          L"Next / previous color theme" },
    { L"F1",                     L"Built-in look: Radial" },
    { L"F6 - F9",                L"Load your saved look  (Shift saves)" },
    { L"P",                      L"Particles on / off" },
    { L"B",                      L"Bloom on / off" },
    { L"G",                      L"Shader effects on / off" },
    { L"M",                      L"Mirror on / off" },
    { L"H",                      L"Hide / show the interface" },
    { L"F  F11  double-click",   L"Fullscreen" },
    { L"R",                      L"Restart audio capture" },
    { L"S",                      L"Connect to Spotify" },
    { L"Shift + S",              L"Disconnect Spotify" },
    { L"Space",                  L"Play / pause" },
    { L"L",                      L"Lyrics on / off" },
    { L"[   ]",                  L"Lyrics later / earlier" },
    { L"Mouse wheel",            L"Change setting / volume / tab" },
};
const int NSC = (int)(sizeof(kShortcuts) / sizeof(kShortcuts[0]));
std::vector<int> gTabRows[5];
int gPTab = 0, gPRow = 0, gPScroll = 0;
float gPSX0 = 0, gPSX1 = 1;
float gMouseX = -1000, gMouseY = -1000;
bool gQueueOpen = false;
int panelRowOfHit(int id) {
    if (id >= H_PR) return id < H_PACT ? id - H_PR : -1;
    if (id >= H_PL) return id - H_PL;
    if (id >= H_PSLD) return id - H_PSLD;
    if (id >= H_PROW) return id - H_PROW;
    return -1;
}
void buildTabs() {
    std::vector<char> used(NP, 0);
    for (int t = 0; t < 5; t++) {
        gTabRows[t].clear();
        for (int k = 0; kPTabItems[t][k]; k++)
            for (int i = 0; i < NP; i++) if (!wcscmp(P[i].name, kPTabItems[t][k])) { gTabRows[t].push_back(i); used[i] = 1; }
    }
    for (int i = 0; i < NP; i++) if (!used[i]) gTabRows[0].push_back(i);      // safety net: nothing is ever unreachable
}
int panelRows() { return gPTab < 5 ? (int)gTabRows[gPTab].size() : (gPTab == 6 ? NSC : 0); }
Param* panelParam() {
    if (gPTab >= 5 || gTabRows[gPTab].empty()) return nullptr;
    gPRow = clampv(gPRow, 0, (int)gTabRows[gPTab].size() - 1);
    return &P[gTabRows[gPTab][gPRow]];
}
void panelTab(int d) { gPTab = (gPTab + d + 7) % 7; gPRow = 0; gPScroll = 0; }
void panelMove(int d) {
    int n = panelRows(); if (n <= 0) return;
    if (gPTab < 5) gPRow = (gPRow + d + n) % n; else if (gPTab == 6) gPScroll = clampv(gPScroll + d, 0, NSC - 1);
}
void panelAdjust(int d, bool big) { if (Param* p = panelParam()) adjust(*p, d, big); else panelTab(d); }
void panelReset() { if (Param* p = panelParam()) *p->v = p->def; }
std::wstring mmss(int ms) { int s = std::max(0, ms / 1000); wchar_t b[16]; swprintf_s(b, L"%d:%02d", s / 60, s % 60); return b; }