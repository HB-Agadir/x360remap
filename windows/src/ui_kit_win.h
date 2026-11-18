#pragma once
// Small drawing kit of the Windows-only pages (tab bar, Controller, Macros, Console,
// In-game display): cards, pills, segmented choices, switches, sliders and vector icons
// on the 1280x720 canvas of the Studio, scaled by kit::g_k. Plain ImGui draw lists.
#include "../../application/third_party/imgui/imgui.h"
#include "../../application/theme.h"

namespace kit {

// kGold is the accent the user picks (SetAccent, saved as accent= in settings.ini), Xbox 360
// green by default; kInk is the text drawn on it, kHover its lighter shade. The console Studio keeps gold.
extern unsigned kGold, kInk, kHover;
const unsigned kTxt = 0xE9EEF4, kTxt2 = 0xA9B8C9, kTxt3 = 0x7F90A4, kGreen = 0x5BE06A;
const int kAccentCount = 6;
unsigned AccentColor(int i);
void     SetAccent(int i);   // out of range = 0 (green)
extern float       g_k;
extern ImDrawList* g_dl;

void   Begin(float canvasW);   // window draw list, scale = canvasW / 1280
ImVec2 P(float x, float y);
ImU32  Col(unsigned rgb, int a = 255);

// align: 0 = left, 1 = centred on x, 2 = right edge at x. y is the top of the text.
float Txt(ImFont* f, float size, float x, float y, unsigned rgb, const char* t, int align = 0, int alpha = 255);
float TxtW(ImFont* f, float size, const char* t);

bool Button(const char* id, float x, float y, float w, float h, bool* hovered = nullptr);
void Card(float x, float y, float w, float h);
void Section(float x, float y, const char* label);
int  Segmented(const char* id, float x, float y, float w, float h, const char* const* labels, int n, int current);
bool Toggle(const char* id, float x, float y, bool on);
bool PillButton(const char* id, float x, float y, float w, float h, const char* label, bool primary);
bool Slider(const char* id, float x, float y, float w, int* value, int minV, int maxV);

enum Icon { IC_KEYBOARD, IC_PAD, IC_MACRO, IC_GAMES, IC_DISPLAY, IC_CONSOLE, IC_HELP, IC_HEART, IC_RECORD, IC_PLAY, IC_PLUS, IC_TRASH };
void DrawIcon(Icon icon, float cx, float cy, float size, unsigned rgb);

}  // namespace kit
