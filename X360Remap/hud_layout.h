#pragma once
#include <stdint.h>

// In-game overlay: the player's own keys become small keycaps. Pure logic, no
// Xbox API: tested by windows/tests/test_windows.cpp.
const int HUD_MAX_CAPS = 24;

struct HudCap {
	uint8_t  code;      // HID usage code, 0xE0..0xE7 for modifiers
	uint8_t  action;    // KeyAction, for the small Xbox button
	char     label[7];
	uint16_t x, y, w;   // relative to the block
};

// Short label of a HID code, upper case. Keypad keys show only their sign, as
// the player reads them: the block must stay small.
void HudKeyLabel(uint8_t code, char* out, int outSize);

// Short Xbox name of a KeyAction, "" when it has none.
const char* HudActionLabel(uint8_t action);

// Fills caps from codes (first HUD_MAX_CAPS, duplicates skipped), rows at most
// maxRowWidth wide, each row indented like the keys of a keyboard.
// fixedWidth: every cap that wide (key images), 0 = sized by the label.
int HudLayout(const uint8_t* codes, const uint8_t* actions, int count, HudCap* caps, int maxRowWidth, int fixedWidth,
              int rowStep, int* totalWidth, int* totalHeight);
