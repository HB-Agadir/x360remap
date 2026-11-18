// cl /nologo /EHsc tests\test_hud_color.cpp && test_hud_color.exe
#include <assert.h>
#include <stdio.h>
#include "../hud_color.h"

// What the GPU shows for a value the CPU wrote, read back the same way the fetch says.
static void Expect(uint32_t got, uint32_t want, const char* what) {
	if (got != want) { printf("%s: got %08x want %08x\n", what, got, want); assert(0); }
}

int main() {
	// Rogue Warrior and the dashboard: 8_8_8_8, endian none, swizzle 1414 (R=Z G=Y B=X A=1).
	// Memory bytes must read B G R A from the lowest address: yellow FFE020 -> 20 E0 FF FF.
	Expect(HudPackColor(0xFFE020, false, 0xfe6a0006, 0x00001414), 0x20E0FFFF, "8888 yellow");
	Expect(HudPackColor(0xFFFFFF, false, 0xfe6a0006, 0x00001414), 0xFFFFFFFF, "8888 white");
	// Metro: 2_10_10_10 (format 0x36), same swizzle: red in bits 20-29 of the little-endian word.
	Expect(HudPackColor(0xFF0000, true, 0xff7a7036, 0x00001414), HudSwap32(0xFFF00000), "2101010 red");
	Expect(HudPackColor(0x0000FF, true, 0xff7a7036, 0x00001414), HudSwap32(0xC00003FF), "2101010 blue");
	// Identity swizzle (R=X G=Y B=Z A=W = 0 1 2 3 -> 0x688 << 1) with endian 8in32: CPU value = GPU word.
	Expect(HudPackColor(0x112233, false, 0x00000086, 0x688 << 1), 0xFF332211, "8888 xyzw 8in32");
	// 8in16 and 16in32 undo the matching swap.
	Expect(HudPackColor(0x112233, false, 0x00000046, 0x688 << 1), 0x2211FF33, "8888 8in16");
	Expect(HudPackColor(0x112233, false, 0x000000C6, 0x688 << 1), 0x33FF1122, "8888 16in32");
	printf("hud color OK\n");
	return 0;
}
