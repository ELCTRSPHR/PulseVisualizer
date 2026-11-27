// ============================================================================
//  PULSE VISUALIZER - desktop audio visualizer + Spotify remote (native C++ / Windows)
//
//  Audio  : WASAPI loopback (captures whatever your PC is playing)
//  Video  : Direct2D 1.1 on Direct3D 11 (float HDR scene, additive blending, album-art backdrop,
//           particles) + HLSL post-processing and shader backgrounds (gpu.cpp)
//  Spotify: Web API over WinHTTP, OAuth PKCE (no client secret), local callback server
//
//  BUILD: open "Pulse Visualizer.slnx" in Visual Studio, Release | x64. Libraries are linked by
//         the #pragmas in common.h.
//
//  SPOTIFY SETUP (one time, ~2 minutes) - press S inside the app for a guided screen
//    * You and the app owner need Spotify Premium (Spotify's 2026 rules)
//    * developer.spotify.com/dashboard > Create app > tick "Web API"
//    * Add Redirect URI exactly:  http://127.0.0.1:8888/callback
//    * Copy the Client ID into the app (Ctrl+V), press Enter, approve in the browser
//
//  CONTROLS
//    Tab         settings panel: tabs Look / Color / Effects / Audio / Spotify / Presets / Keys
//                (Up/Down pick, Left/Right change, PgUp/PgDn or Q/E switch tab, click works too)
//    1-3         visual (Bars / Radial / NCS)      C / Shift+C  color theme    F1  built-in look (Radial)
//    F6-F9       load your preset slot (Shift+F6-F9 saves)
//    P particles  B bloom  G shader effects  M mirror  H hide UI  F / F11 / double-click fullscreen
//    S           connect Spotify (Shift+S disconnects)
//    Space       play/pause   Left/Right prev/next (panel closed)   Up/Down volume
//    Mouse       click the now-playing card: buttons, seek bar, volume slider
//    R           restart audio capture
//    L           lyrics on/off      [ and ]  nudge lyric timing (later / earlier)
//  Lyrics are fetched from LRCLIB (lrclib.net), matched to the Spotify track.
//  In Radial the vinyl slides left and the lyrics take a right-hand column (Tab > Lyrics).
//  Settings auto-save to rgbpulse.ini next to the exe.
// ============================================================================

#include "common.h"
#include "util.h"
#include "settings.h"
#include "color.h"
#include "audio.h"
#include "analyzer.h"
#include "spotify.h"
#include "ui_state.h"
#include "renderer.h"
#include "input.h"

static bool sMouseTracked = false;
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_MOUSELEAVE:                                 // mouse left the window: drop hover states (card shrinks back, highlights clear)
        sMouseTracked = false;
        if (!gDrag) { gMouseX = -1000.f; gMouseY = -1000.f; }
        return 0;
    case WM_SIZE:
        gMinimized = (wp == SIZE_MINIMIZED);
        if (!gMinimized) { gPendW = LOWORD(lp); gPendH = HIWORD(lp); }
        return 0;
    case WM_MOUSEMOVE:
        gLastInput = nowSec();
        gMouseX = (float)GET_X_LPARAM(lp); gMouseY = (float)GET_Y_LPARAM(lp);
        if (!sMouseTracked) { TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 }; sMouseTracked = TrackMouseEvent(&tme) != FALSE; }   // so we hear WM_MOUSELEAVE
        if (gDrag) dragUpdate(gMouseX);
        else if (gPanel && gPTab < 5) { int idx = panelRowOfHit(hitTest(gMouseX, gMouseY)); if (idx >= 0 && idx < panelRows()) gPRow = idx; }   // selection follows the mouse
        return 0;
    case WM_RBUTTONDOWN: {                              // right-click a setting = reset it
        gLastInput = nowSec();
        int idx = panelRowOfHit(hitTest((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp)));
        if (gPanel && gPTab < 5 && idx >= 0 && idx < panelRows()) { gPRow = idx; panelReset(); toast(L"Reset to default"); }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        gLastInput = nowSec(); skipSplash();
        float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
        int id = hitTest(x, y);
        if (id > 0) handleClick(id, x);
        else if (gOverlay != OV_NONE) closeOverlay();
        return 0;
    }
    case WM_LBUTTONUP:
        if (gDrag) {
            if (gDrag == H_PROG) { PlayerState s = gSp.state(); gSp.seek((int)(gSeekFrac * s.t.durMs)); gSeekUntil = nowSec() + 1.3; }
            gDrag = 0; ReleaseCapture();
        }
        return 0;
    case WM_LBUTTONDBLCLK: {
        float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
        int id = hitTest(x, y);
        gLastInput = nowSec();
        if (id > 0 && id != H_PANEL) handleClick(id, x);         // a fast second click on a button still counts as a click
        else if (id < 0 && gOverlay == OV_NONE) toggleFullscreen();
        return 0;
    }
    case WM_MOUSEWHEEL: {
        gLastInput = nowSec(); int dir = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(h, &pt);
        if (hitTest((float)pt.x, (float)pt.y) == H_VOL) changeVolume(dir * 5);
        else if (gPanel) {
            int id = hitTest((float)pt.x, (float)pt.y);
            if (id >= H_PTAB && id < H_PROW) panelTab(-dir);                          // wheel over the tab strip = switch tab
            else if (gPTab < 5) { int idx = panelRowOfHit(id); if (idx >= 0 && idx < panelRows()) gPRow = idx; panelAdjust(dir, (GetKeyState(VK_SHIFT) & 0x8000) != 0); }
            else if (gPTab == 6) panelMove(-dir);
        }
        return 0;
    }
    case WM_CHAR:
        if (gOverlay != OV_NONE) {
            wchar_t c = (wchar_t)wp;
            if (c == 8) { if (!gInput.empty()) gInput.pop_back(); }
            else if (c == 22) {                       // Ctrl+V
                std::wstring clip = clipGet(), clean;
                for (wchar_t ch : clip) if (ch >= 32) clean += ch;
                if (gInput.size() + clean.size() < 240) gInput += clean;
            }
            else if (c >= 32 && gInput.size() < 240) gInput += c;
        }
        return 0;
    case WM_KEYDOWN: {
        gLastInput = nowSec(); skipSplash();
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (gOverlay != OV_NONE) {
            switch (wp) {
            case VK_ESCAPE: closeOverlay(); break;
            case VK_RETURN: overlayEnter(); break;
            case VK_DELETE: gInput.clear(); break;
            }
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && (wp == 'C' || wp == 'V')) { if (wp == 'C') exportLook(); else importLook(); return 0; }
        switch (wp) {
        case VK_TAB: gPanel = !gPanel; gShowUI = true; if (gPanel) refreshSlots(); break;
        case VK_PRIOR: if (gPanel) panelTab(-1); break;
        case VK_NEXT:  if (gPanel) panelTab(1); break;
        case 'Q': if (gPanel) panelTab(-1); break;
        case 'E': if (gPanel) panelTab(1); break;
        case VK_UP:   if (gPanel) panelMove(-1); else changeVolume(5); break;
        case VK_DOWN: if (gPanel) panelMove(1); else changeVolume(-5); break;
        case VK_LEFT:  if (gPanel) panelAdjust(-1, shift); else gSp.prev(); break;
        case VK_RIGHT: if (gPanel) panelAdjust(1, shift); else gSp.next(); break;
        case VK_BACK:  if (gPanel) panelReset(); break;
        case VK_SPACE: if (gSp.status == Spotify::ST_ON) gSp.togglePlay(); break;
        case VK_F11: case 'F': toggleFullscreen(); break;
        case VK_ESCAPE: if (gPanel) gPanel = false; else if (gFullscreen) toggleFullscreen(); break;
        case 'H': gShowUI = !gShowUI; break;
        case 'L': S.lyrics = S.lyrics > 0.5f ? 0.f : 1.f; toast(S.lyrics > 0.5f ? L"Lyrics on" : L"Lyrics off"); break;
        case 'T': openDeepL(); break;
        case VK_OEM_4: S.lyricOffset = clampv(S.lyricOffset - 100.f, -3000.f, 3000.f); toast(L"Lyrics later: offset " + std::to_wstring((int)S.lyricOffset) + L" ms"); break;
        case VK_OEM_6: S.lyricOffset = clampv(S.lyricOffset + 100.f, -3000.f, 3000.f); toast(L"Lyrics earlier: offset " + std::to_wstring((int)S.lyricOffset) + L" ms"); break;
        case 'R': au.restart(); toast(L"Restarting audio capture"); break;
        case 'S':
            if (shift) { gSp.disconnect(); toast(L"Spotify disconnected"); }
            else connectSpotify();
            break;
        case 'C': adjust(P[1], shift ? -1 : 1, false); toast(std::wstring(L"Colors: ") + kTheme[(int)(S.theme + 0.5f)]); break;
        case 'P': S.particles = S.particles > 0.5f ? 0.f : 1.f; break;
        case 'M': S.mirror = S.mirror > 0.5f ? 0.f : 1.f; break;
        case 'B': S.bloom = S.bloom > 0.01f ? 0.f : 1.f; break;
        case 'G':
            if (!gR.shadersOk()) { toast(L"Shader effects are not available on this GPU"); break; }
            S.fx = S.fx > 0.5f ? 0.f : 1.f; toast(S.fx > 0.5f ? L"Shader effects on" : L"Shader effects off"); break;
        default:
            if (wp >= '1' && wp <= '3') S.mode = (float)(wp - '1');
            else if (wp >= VK_F1 && wp < VK_F1 + NPRESET) applyPreset((int)(wp - VK_F1));
            else if (wp >= VK_F6 && wp <= VK_F9) {
                std::wstring sec = L"slot" + std::to_wstring((int)(wp - VK_F6) + 1);
                if (shift) { saveSection(sec); refreshSlots(); toast(L"Saved to slot " + std::to_wstring((int)(wp - VK_F6) + 1)); }
                else if (loadSection(sec)) toast(L"Loaded slot " + std::to_wstring((int)(wp - VK_F6) + 1));
                else toast(L"Slot is empty - Shift+F-key saves your current look");
            }
        }
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
static HICON makeIcon(int sz) {                              // same logo, software-rendered for the title bar / taskbar
    std::vector<uint32_t> px((size_t)sz * sz, 0);
    const int SS = 3; const float c = sz * 0.5f, rad = sz * 0.22f, N = 24.f;
    for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
        float ar = 0, ag = 0, ab = 0, aa = 0;
        for (int sy = 0; sy < SS; sy++) for (int sx = 0; sx < SS; sx++) {
            float fx = x + (sx + 0.5f) / SS - c, fy = y + (sy + 0.5f) / SS - c;
            float qx = fabsf(fx) - (c - rad), qy = fabsf(fy) - (c - rad), mx = std::max(qx, 0.f), my = std::max(qy, 0.f);
            if (sqrtf(mx * mx + my * my) + std::min(std::max(qx, qy), 0.f) - rad > 0.f) continue;
            float r = 0.035f, g = 0.035f, b = 0.07f, rr = sqrtf(fx * fx + fy * fy) / sz, ang = atan2f(fy, fx);
            float u = (ang + PI) / (2.f * PI) * N; int i = (int)u; float f = u - i;
            float wv = 0.5f + 0.5f * sinf(i * 0.9f + 1.f) * cosf(i * 0.37f), r0 = 0.20f, len = 0.05f + 0.17f * wv;
            if (f > 0.16f && f < 0.84f && rr > r0 && rr < r0 + len) { D2D1_COLOR_F h = hsv(i / N * 0.83f + 0.02f, 0.9f, 1.f, 1.f); r = h.r; g = h.g; b = h.b; }
            else if ((rr > 0.115f && rr < 0.14f) || rr < 0.05f) { r = g = b = 0.95f; }
            ar += r; ag += g; ab += b; aa += 1.f;
        }
        if (aa > 0.f) {
            float inv = 1.f / aa, a = aa / (float)(SS * SS);
            px[(size_t)y * sz + x] = ((uint32_t)(a * 255.f) << 24) | ((uint32_t)(clampv(ar * inv, 0.f, 1.f) * 255.f) << 16) | ((uint32_t)(clampv(ag * inv, 0.f, 1.f) * 255.f) << 8) | (uint32_t)(clampv(ab * inv, 0.f, 1.f) * 255.f);
        }
    }
    BITMAPV5HEADER bi = {}; bi.bV5Size = sizeof(bi); bi.bV5Width = sz; bi.bV5Height = -sz; bi.bV5Planes = 1; bi.bV5BitCount = 32; bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00; bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr; HDC dc = GetDC(nullptr);
    HBITMAP col = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0); ReleaseDC(nullptr, dc);
    if (!col || !bits) return nullptr;
    memcpy(bits, px.data(), px.size() * 4);
    HBITMAP mask = CreateBitmap(sz, sz, 1, 1, nullptr); ICONINFO ii = {}; ii.fIcon = TRUE; ii.hbmMask = mask; ii.hbmColor = col;
    HICON ic = CreateIconIndirect(&ii); DeleteObject(col); DeleteObject(mask); return ic;
}
int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int show) {
    SetProcessDPIAware();
    for (int i = 0; i < NP; i++) P[i].def = *P[i].v;
    loadSection(L"settings"); refreshSlots();
    buildTabs();

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS; wc.lpfnWndProc = WndProc; wc.hInstance = hi;
    wc.hIcon = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"PulseVisualizerWnd";
    RegisterClassExW(&wc);
    gHwnd = CreateWindowExW(0, wc.lpszClassName, L"Pulse Visualizer", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760, nullptr, nullptr, hi, nullptr);
    if (!wc.hIcon) if (HICON big = makeIcon(64)) SendMessageW(gHwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
    if (!wc.hIconSm) if (HICON sm = makeIcon(32)) SendMessageW(gHwnd, WM_SETICON, ICON_SMALL, (LPARAM)sm);
    ShowWindow(gHwnd, show); UpdateWindow(gHwnd);

    if (!gR.init(gHwnd)) { MessageBoxW(gHwnd, L"Could not initialise Direct3D 11 / Direct2D.", L"Pulse Visualizer", MB_ICONERROR); return 1; }
    toggleFullscreen();                                                   // launch in full screen (F11 / Esc goes back to a window)
    au.start();
    gSp.start();
    gSplashT0 = nowSec();

    gLastInput = nowSec();
    double prev = nowSec(); bool cursorHidden = false; MSG msg = {};
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (gMinimized) { Sleep(30); prev = nowSec(); continue; }
        if (gPendW && (gPendW != gR.W || gPendH != gR.H)) gR.resize(gPendW, gPendH);

        double t = nowSec(); float dt = clampv((float)(t - prev), 0.0005f, 0.1f); prev = t;
        bool wantHide = gFullscreen && (t - gLastInput) > 2.5 && gOverlay == OV_NONE && !gPanel && !gDrag;
        if (wantHide && !cursorHidden) { ShowCursor(FALSE); cursorHidden = true; }
        else if (!wantHide && cursorHidden) { ShowCursor(TRUE); cursorHidden = false; }

        an.update(dt);
        gR.render(dt);
    }
done:
    gSp.stop();
    au.stop();
    saveSection(L"settings");
    return 0;
}