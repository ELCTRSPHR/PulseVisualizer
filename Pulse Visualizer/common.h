// Shared includes, link pragmas and constants. Every .cpp includes this first.
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#undef DrawText                       // windows.h maps DrawText -> DrawTextW and breaks Direct2D
#include <shellapi.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <wincodec.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d2d1effects.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>
#include <vector>
#include <map>
#include <set>
#include <complex>
#include <cmath>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cwchar>
#include <string>
#include <chrono>
#include <algorithm>
#include <random>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

using Microsoft::WRL::ComPtr;
static const float PI = 3.14159265358979f;
constexpr int    FFT_N = 4096, MAXB = 256, HIST_N = 40;
constexpr size_t RING = 1 << 15;
template <class T> static T clampv(T v, T a, T b) { return v < a ? a : (v > b ? b : v); }