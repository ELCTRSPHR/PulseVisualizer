// Input actions: clicks, volume, fullscreen, Spotify setup screen
#include "common.h"
#include "util.h"
#include "http.h"
#include "settings.h"
#include "spotify.h"
#include "ui_state.h"
#include "input.h"

// ============================================================================
//  SPOTIFY UI ACTIONS + WINDOW
// ============================================================================
void closeOverlay() { gOverlay = OV_NONE; gInput.clear(); gDrag = 0; }
void connectSpotify() {
    int st = gSp.status;
    if (st == Spotify::ST_ON) toast(L"Spotify is connected");
    else if (!gSp.haveId) { gOverlay = OV_SETUP; gInput.clear(); gShowUI = true; }
    else if (st == Spotify::ST_WAIT) toast(L"Waiting for you to approve access in the browser...");
    else { gSp.connect(); toast(L"Opening Spotify sign-in in your browser..."); }
}
void openDeepL() { gOverlay = OV_DEEPL; gInput.clear(); gShowUI = true; gPanel = false; }
void overlayEnter() {
    if (gOverlay == OV_SETUP) {
        if (gInput.empty()) return;
        gSp.setClientId(toU8(gInput)); gSp.connect(); closeOverlay(); toast(L"Approve access in your browser");
    }
    else if (gOverlay == OV_DEEPL) {
        if (gInput.empty()) {                         // Enter on an empty field removes a saved key
            if (gSp.haveDeepL) { gSp.setDeepLKey(""); toast(L"DeepL key removed"); }
            closeOverlay(); return;
        }
        gSp.setDeepLKey(toU8(gInput)); S.translate = 1.f; closeOverlay(); toast(L"Checking your DeepL key...");
    }
}
int hitTest(float x, float y) {
    for (int i = (int)gHits.size() - 1; i >= 0; i--) { const Hit& h = gHits[i]; if (x >= h.x0 && x <= h.x1 && y >= h.y0 && y <= h.y1) return h.id; }
    return -1;
}
void dragUpdate(float x) {
    if (gDrag == H_VOL) { float f = clampv((x - gVolX0) / (gVolX1 - gVolX0), 0.f, 1.f); gVol = (int)(f * 100.f + 0.5f); gVolUntil = nowSec() + 2.5; gSp.setVolume(gVol); }
    else if (gDrag == H_PROG) gSeekFrac = clampv((x - gProgX0) / (gProgX1 - gProgX0), 0.f, 1.f);
    else if (gDrag == H_PSLD) {
        Param* pp = panelParam();
        if (pp && !pp->labels) { float f = clampv((x - gPSX0) / (gPSX1 - gPSX0), 0.f, 1.f); float v = pp->mn + f * (pp->mx - pp->mn); v = roundf(v / pp->st) * pp->st; *pp->v = clampv(v, pp->mn, pp->mx); }
    }
}
void changeVolume(int delta) {
    PlayerState s = gSp.state(); double now = nowSec();
    int cur = now < gVolUntil ? gVol : s.volume;
    gVol = clampv(cur + delta, 0, 100); gVolUntil = now + 2.5; gSp.setVolume(gVol);
}
void toggleFullscreen() {
    static WINDOWPLACEMENT wp = { sizeof(wp) };
    DWORD st = GetWindowLongW(gHwnd, GWL_STYLE);
    if (!gFullscreen) {
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(gHwnd, &wp) && GetMonitorInfoW(MonitorFromWindow(gHwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongW(gHwnd, GWL_STYLE, st & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(gHwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            gFullscreen = true;
        }
    }
    else {
        SetWindowLongW(gHwnd, GWL_STYLE, st | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(gHwnd, &wp);
        SetWindowPos(gHwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        gFullscreen = false;
    }
}
void exportLook() { clipSet(exportCode()); toast(L"Look code copied - send it to a friend!"); }
void importLook() {
    if (importCode(clipGet())) toast(L"Look imported!");
    else toast(L"No look code found on your clipboard");
}
static void panelAction(int a) {
    if (a < NPRESET) { applyPreset(a); return; }
    if (a >= 10 && a < 14) {
        std::wstring n = std::to_wstring(a - 9);
        if (loadSection(L"slot" + n)) toast(L"Loaded slot " + n); else toast(L"Slot " + n + L" is empty"); return;
    }
    if (a >= 20 && a < 24) { std::wstring n = std::to_wstring(a - 19); saveSection(L"slot" + n); refreshSlots(); toast(L"Saved to slot " + n); return; }
    if (a == 30) exportLook(); else if (a == 31) importLook();
    else if (a == 32) gPanel = false;
    else if (a == 40) panelTab(-1); else if (a == 41) panelTab(1);
}
void handleClick(int id, float x) {
    PlayerState s = gSp.state();
    switch (id) {
    case H_PREV: gSp.prev(); break;
    case H_NEXT: gSp.next(); break;
    case H_PLAY: gSp.togglePlay(); break;
    case H_SHUF: gSp.setShuffle(!s.shuffle); break;
    case H_REP:  gSp.setRepeat(s.repeat == "off" ? "context" : (s.repeat == "context" ? "track" : "off")); break;
    case H_CONNECT: connectSpotify(); break;
    case H_VOL: case H_PROG: gDrag = id; SetCapture(gHwnd); dragUpdate(x); break;
    case H_COPY: clipSet(L"http://127.0.0.1:8888/callback"); toast(L"Redirect URI copied"); break;
    case H_Q0: case H_Q1: case H_Q2: gSp.skipTo(id - H_Q0); break;
    case H_QBTN: gQueueOpen = !gQueueOpen; break;
    default:
        if (id >= H_PACT) panelAction(id - H_PACT);
        else if (id >= H_PL) { bool big = (GetKeyState(VK_SHIFT) & 0x8000) != 0; bool right = id >= H_PR; gPRow = id - (right ? H_PR : H_PL); if (Param* pp = panelParam()) adjust(*pp, right ? 1 : -1, big); }
        else if (id >= H_PSLD) { gPRow = id - H_PSLD; gDrag = H_PSLD; SetCapture(gHwnd); dragUpdate(x); }
        else if (id >= H_PROW) gPRow = id - H_PROW;
        else if (id >= H_PTAB) { gPTab = id - H_PTAB; gPRow = 0; gPScroll = 0; }
        break;
    }
}