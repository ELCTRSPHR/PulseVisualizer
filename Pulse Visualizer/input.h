// Input actions: clicks, volume, fullscreen, Spotify setup screen
#pragma once
#include "common.h"

void closeOverlay();
void connectSpotify();              // S key / "Connect Spotify" button
void overlayEnter();
int hitTest(float x, float y);
void dragUpdate(float x);
void changeVolume(int delta);
void toggleFullscreen();
void handleClick(int id, float x);
void exportLook();                  // copy share code to clipboard
void importLook();                  // read share code from clipboard