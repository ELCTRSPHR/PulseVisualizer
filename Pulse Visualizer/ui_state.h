// UI state, settings-panel model, clipboard, splash timing
#pragma once
#include "common.h"
#include "settings.h"

// ============================================================================
//  UI STATE
// ============================================================================
enum { OV_NONE, OV_SETUP, OV_DEEPL };          // OV_SETUP: Spotify Client ID screen, OV_DEEPL: DeepL key screen
enum { H_PANEL = 1, H_PREV, H_PLAY, H_NEXT, H_SHUF, H_REP, H_PROG, H_VOL, H_CONNECT, H_COPY, H_Q0, H_Q1, H_Q2, H_QBTN, H_PTAB = 2000, H_PROW = 3000, H_PSLD = 4000, H_PL = 5000, H_PR = 6000, H_PACT = 7000 };
struct Hit { float x0, y0, x1, y1; int id; };
extern std::vector<Hit> gHits;
extern HWND gHwnd;
extern bool gShowUI, gPanel, gFullscreen, gMinimized;
extern int gPendW, gPendH, gOverlay, gDrag, gVol;
extern double gLastInput, gTrackT, gVolUntil, gSeekUntil;
extern float gSeekFrac, gProgX0, gProgX1, gVolX0, gVolX1;
extern std::wstring gInput;
extern double gSplashT0;
static const float gSplashDur = 5.4f, gSplashOut = 1.0f;
float smooth01(float x);
float easeBack(float x);                         // 0..1 -> 0..1 with a small overshoot (pop-in)
extern bool gSplashDone;
void skipSplash();
std::wstring clipGet();
void clipSet(const std::wstring& s);
extern const wchar_t* const kPTabNames[7];
extern const wchar_t* const kPTabItems[5][20];
struct Shortcut { const wchar_t* key; const wchar_t* what; };
extern const Shortcut kShortcuts[];
extern const int NSC;
extern std::vector<int> gTabRows[5];
extern int gPTab, gPRow, gPScroll;
extern float gPSX0, gPSX1;
extern bool gQueueOpen;                         // "Up next" list toggled from the now-playing card
extern float gMouseX, gMouseY;                  // last mouse position (hover effects)
int panelRowOfHit(int hitId);                    // settings row under a hit id, or -1
void buildTabs();
int panelRows();
Param* panelParam();
void panelTab(int d);
void panelMove(int d);
void panelAdjust(int d, bool big);
void panelReset();
std::wstring mmss(int ms);