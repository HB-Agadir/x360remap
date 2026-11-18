#include "hud_layout.h"

#include <stdio.h>
#include <string.h>

static const int CAP_HEIGHT = 30, CAP_GAP = 6, ROW_INDENT = 6;
static const int CHAR_WIDTH = 10, CAP_PADDING = 34, CAP_MIN_WIDTH = 48;

const char* HudActionLabel(uint8_t action) {
	// Same order as enum KeyAction in mapping.h.
	static const char* const kNames[] = { "", "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "L3", "R3", "ST", "BK", "G",
		"D^", "Dv", "D<", "D>", "L^", "Lv", "L<", "L>", "R^", "Rv", "R<", "R>" };
	return action < sizeof(kNames) / sizeof(kNames[0]) ? kNames[action] : "";
}

void HudKeyLabel(uint8_t code, char* out, int outSize) {
	static const struct { uint8_t code; const char* text; } kNames[] = {
		{0x28, "ENTER"}, {0x29, "ESC"},  {0x2A, "BKSP"}, {0x2B, "TAB"},  {0x2C, "SPACE"},
		{0x49, "INS"},   {0x4A, "HOME"}, {0x4B, "PGUP"}, {0x4C, "DEL"},  {0x4D, "END"},   {0x4E, "PGDN"},
		{0x4F, ">"},     {0x50, "<"},    {0x51, "v"},    {0x52, "^"},
		{0x54, "/"},     {0x55, "*"},    {0x56, "-"},    {0x57, "+"},    {0x58, "ENTER"}, {0x63, "."},
		{0xE0, "CTRL"},  {0xE1, "SHIFT"},{0xE2, "ALT"},  {0xE3, "WIN"},
		{0xE4, "RCTRL"}, {0xE5, "RSHFT"},{0xE6, "RALT"}, {0xE7, "RWIN"},
	};
	for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++)
		if (kNames[i].code == code) { _snprintf(out, outSize - 1, "%s", kNames[i].text); out[outSize - 1] = 0; return; }
	if (code >= 0x04 && code <= 0x1D) _snprintf(out, outSize - 1, "%c", 'A' + (code - 0x04));
	else if (code >= 0x1E && code <= 0x26) _snprintf(out, outSize - 1, "%c", '1' + (code - 0x1E));
	else if (code == 0x27 || code == 0x62) _snprintf(out, outSize - 1, "0");
	else if (code >= 0x59 && code <= 0x61) _snprintf(out, outSize - 1, "%c", '1' + (code - 0x59));
	else if (code >= 0x3A && code <= 0x45) _snprintf(out, outSize - 1, "F%d", 1 + (code - 0x3A));
	else _snprintf(out, outSize - 1, "%02X", (unsigned)code);
	out[outSize - 1] = 0;
}

int HudLayout(const uint8_t* codes, const uint8_t* actions, int count, HudCap* caps, int maxRowWidth, int fixedWidth,
              int rowStep, int* totalWidth, int* totalHeight) {
	int n = 0, x = 0, row = 0, widest = 0;
	for (int i = 0; i < count && n < HUD_MAX_CAPS; i++) {
		bool seen = false;
		for (int k = 0; k < n; k++) seen = seen || caps[k].code == codes[i];
		if (seen) continue;
		HudCap& c = caps[n];
		c.code = codes[i];
		c.action = actions[i];
		HudKeyLabel(codes[i], c.label, sizeof(c.label));
		int w = (int)strlen(c.label) * CHAR_WIDTH + CAP_PADDING;
		if (w < CAP_MIN_WIDTH) w = CAP_MIN_WIDTH;
		if (fixedWidth) w = fixedWidth;
		if (x > 0 && row * ROW_INDENT + x + w > maxRowWidth) { x = 0; row++; }
		c.x = (uint16_t)(row * ROW_INDENT + x);
		c.y = (uint16_t)(row * rowStep);
		c.w = (uint16_t)w;
		x += w + CAP_GAP;
		if (c.x + w > widest) widest = c.x + w;
		n++;
	}
	if (totalWidth) *totalWidth = widest;
	if (totalHeight) *totalHeight = n ? (row + 1) * rowStep - (rowStep - CAP_HEIGHT) : 0;
	return n;
}
