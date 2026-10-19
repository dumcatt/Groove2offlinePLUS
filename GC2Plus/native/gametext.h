// Text in the game's own pixel font (UV font 0), drawn through MtxGraphics3D.
// Implemented in fastlate.cpp. Coordinates are draw-screen units
// (GetDrawScreenWidth x GetDrawScreenHeight, origin top-left); colours RGBA.
#pragma once

constexpr int kTextAlignLeft = 0x10;    // vertically centred, starting at x
constexpr int kTextAlignCenter = 0x12;  // centred on (x, y)

// False until the game's drawing functions were found.
bool game_text_ready();
float game_screen_width();
float game_screen_height();
// Size of `text` at scale 1.
void game_text_size(void* g, const char* text, float* w, float* h);
void draw_game_text(void* g, const char* text, float x, float y, float scale, unsigned rgba, int align);
