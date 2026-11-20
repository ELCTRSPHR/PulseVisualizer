// Color helpers (HSV, themes, album palette)
#pragma once
#include "common.h"

extern float gBright, gHueOff;
extern float gAlbumCur[3][3];
extern float gAlbumTgt[3][3];
D2D1_COLOR_F hsv(float h, float s, float v, float a);
D2D1_COLOR_F palette(int theme, float t, float phase, float a = 1.f);
D2D1_COLOR_F whiten(D2D1_COLOR_F c, float t);