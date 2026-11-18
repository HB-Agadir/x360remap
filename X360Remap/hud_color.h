#pragma once
// Packs an RRGGBB colour into the front buffer's own pixel layout, read from the
// VdSwap fetch constant (Xenia's xenos.h, xe_gpu_texture_fetch_t): dword_1 bits
// 6-7 = endian (0 none, 1 8in16, 2 8in32, 3 16in32), dword_3 bits 1-12 = swizzle,
// 3 bits per output R G B A, value 0-3 = memory component X Y Z W (4/5 = 0/1).
// Before this, every game showed the Studio colours with red and blue swapped
// (Rogue Warrior, Metro: fetch dword_3 1414 = R from Z, B from X, endian none).
// Header only: tests/test_hud_color.cpp.
#include <stdint.h>

static uint32_t HudSwap32(uint32_t v) { return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24); }

// wide = 2_10_10_10 (components 10 10 10 2 bits), otherwise 8_8_8_8.
// Returns the 32-bit value the CPU writes (big-endian) to show that colour.
static uint32_t HudPackColor(uint32_t rgb, bool wide, uint32_t fetch1, uint32_t fetch3) {
	const uint32_t ch[4] = { (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0xFF };   // R G B A
	const uint32_t swizzle = (fetch3 >> 1) & 0xFFF;
	uint32_t gpu = 0, used = 0;
	for (int c = 0; c < 4; c++) {
		const uint32_t src = (swizzle >> (c * 3)) & 7;
		if (src > 3) continue;
		const uint32_t bits = wide ? (src == 3 ? 2 : 10) : 8, shift = wide ? src * 10 : src * 8;
		const uint32_t v = bits == 10 ? (ch[c] << 2) | (ch[c] >> 6) : bits == 2 ? ch[c] >> 6 : ch[c];
		gpu |= v << shift;
		used |= 1u << src;
	}
	// A component nothing reads (alpha set to 1 by the swizzle): fully on.
	for (uint32_t src = 0; src < 4; src++)
		if (!(used & (1u << src))) gpu |= (wide ? (src == 3 ? 3u : 0x3FFu) : 0xFFu) << (wide ? src * 10 : src * 8);
	// The GPU reads memory as little-endian, then applies the endian swap.
	switch ((fetch1 >> 6) & 3) {
	case 0: return HudSwap32(gpu);
	case 1: return (gpu >> 16) | (gpu << 16);                                   // 8in16
	case 2: return gpu;                                                          // 8in32
	default: return ((gpu >> 8) & 0x00FF00FF) | ((gpu << 8) & 0xFF00FF00);       // 16in32
	}
}
