# Pulse Visualizer

A native Windows music visualizer with a Spotify remote and synced lyrics.
It listens to whatever your PC is playing (WASAPI loopback), so it works with any player.

- **Visuals:** Bars, Radial (spinning album-art record) and an NCS-style neon ring
- **GPU effects:** HDR bloom with light streaks, chromatic aberration on kicks, tone mapping,
  color grading, film grain, and optional music-reactive backgrounds (Flow, Nebula, Aurora)
- **Spotify:** now-playing card with play/pause, skip, seek, volume and an up-next queue
- **Lyrics:** time-synced lyrics from [LRCLIB](https://lrclib.net) with a glowing current line
- **Colors:** themes, or colors pulled from the current album art
- **Looks:** save up to 4 looks, or share one as a short `PV1-...` code

Plain C++17 on Win32, Direct2D, Direct3D 11 (HLSL), DirectWrite, WIC and WinHTTP.
No third-party libraries; it builds to a single exe.

## Requirements

- Windows 10 or 11, x64, with a Direct3D 11 capable GPU
- Visual Studio 2026 with the **Desktop development with C++** workload
  (Visual Studio 2022 17.13+ also works: when it asks, retarget the project to its v143 toolset)

## Building

1. Open `Pulse Visualizer.slnx`.
2. Select **Release | x64** and build.
3. The exe is written to `x64\Release\`.

Close the app before rebuilding, or the linker can't overwrite the exe.

## Spotify setup (one time)

Spotify playback control needs a free developer app and a Premium account.

1. Go to the [Spotify developer dashboard](https://developer.spotify.com/dashboard) and create an app
   with **Web API** enabled.
2. Add this redirect URI exactly: `http://127.0.0.1:8888/callback`
3. In Pulse Visualizer press **S**, paste the app's Client ID (Ctrl+V), press Enter and approve in the browser.

Sign-in uses OAuth PKCE, so no client secret is needed. The refresh token is stored encrypted for
your Windows user (DPAPI) in `rgbpulse.ini` next to the exe, together with your settings.

## Controls

| Key | Action |
| --- | --- |
| Tab / Esc | Open / close settings |
| Q / E, PgUp / PgDn | Switch settings tab |
| Up / Down | Pick a setting (volume when settings are closed) |
| Left / Right | Change a setting (previous / next track when closed) |
| Backspace, right-click | Reset the selected setting |
| 1 2 3 | Bars / Radial / NCS |
| C, Shift+C | Next / previous color theme |
| F1 | Built-in look |
| F6-F9, Shift+F6-F9 | Load / save your looks |
| Ctrl+C, Ctrl+V | Copy / paste a look code |
| G | Shader effects on / off |
| B, P, M | Bloom, particles, mirror |
| L, [ and ] | Lyrics on / off, nudge lyric timing |
| H | Hide / show the interface |
| F, F11, double-click | Fullscreen |
| Space | Play / pause |
| S, Shift+S | Connect / disconnect Spotify |
| R | Restart audio capture |

## Source layout

| File | Purpose |
| --- | --- |
| `main.cpp` | Window, message loop, keyboard and mouse |
| `renderer.cpp` | Direct2D / Direct3D setup and frame composition |
| `gpu.cpp` | HLSL post-processing and shader backgrounds |
| `visuals.cpp` | Bars, Radial, NCS ring, particles, album art |
| `lyrics_render.cpp` | Lyric layout, timing and drawing |
| `ui_draw.cpp`, `ui_state.cpp` | Now-playing card, settings panel, UI state |
| `splash.cpp` | Logo and splash screen |
| `audio.cpp`, `analyzer.cpp` | Loopback capture, FFT, bands and beat detection |
| `spotify.cpp` | Spotify Web API, album art, lyrics lookup |
| `settings.cpp` | Settings table, presets, ini file, look codes |
| `color.cpp` | Color themes and album-art palette |
| `http.cpp`, `json.cpp`, `util.cpp` | WinHTTP wrapper, small JSON parser, helpers |
| `input.cpp` | Click handling, volume, fullscreen |

## Notes

The NCS mode is a tribute to the NoCopyrightSounds visualizer style. This project is not affiliated
with NoCopyrightSounds or Spotify.
