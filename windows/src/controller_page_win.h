#pragma once
#include <stdint.h>
#include <string>
// Controller page of the Windows Studio (tab 5): see controller_page_win.cpp.
void DrawControllerPageWin(float w, float h, int& tab);
// Controller shown on that page (also the one the Macros page edits). False if none.
bool ControllerPageCurrentPad(uint16_t& vid, uint16_t& pid, std::string& name);
